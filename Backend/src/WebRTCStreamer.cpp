#include "WebRTCStreamer.hpp"

namespace routing {

static void post(GMainContext* ctx, GSourceFunc fn, gpointer data)
{
    GSource* src = g_idle_source_new();
    g_source_set_priority(src, G_PRIORITY_DEFAULT);
    g_source_set_callback(src, fn, data, nullptr);
    g_source_attach(src, ctx);
    g_source_unref(src);
}

static gboolean cb_create_offer(gpointer data)
{
    auto* self = static_cast<WebRTCStreamer*>(data);
    std::cout << "[Stream " << self->id_ << "] create_offer\n";
    self->create_offer();
    return G_SOURCE_REMOVE;
}

static gboolean cb_bus_watch(GstBus*, GstMessage* msg, gpointer data)
{
    auto* self = static_cast<WebRTCStreamer*>(data);

    switch (GST_MESSAGE_TYPE(msg))
    {
        case GST_MESSAGE_STATE_CHANGED:
        {
            if (GST_MESSAGE_SRC(msg) == GST_OBJECT(self->pipeline_))
            {
                GstState old_state, new_state, pending;
                gst_message_parse_state_changed(msg, &old_state, &new_state, &pending);
                std::cout << "[Stream " << self->id_ << "] state "
                          << gst_element_state_get_name(old_state) << " -> "
                          << gst_element_state_get_name(new_state) << "\n";

                if (new_state == GST_STATE_PLAYING && !self->offer_created_)
                {
                    self->offer_created_ = true;
                    // Re-acquire webrtc_ now pipeline is fully up
                    if (self->webrtc_) { gst_object_unref(self->webrtc_); }
                    self->webrtc_ = gst_bin_get_by_name(GST_BIN(self->pipeline_), "webrtc");
                    if (!self->webrtc_) {
                        std::cerr << "[Stream " << self->id_ << "] webrtc_ null at PLAYING\n";
                        break;
                    }
                    std::cout << "[Stream " << self->id_ << "] PLAYING -> posting create_offer\n";
                    post(self->ctx_, cb_create_offer, self);
                }
            }
            break;
        }
        case GST_MESSAGE_ERROR:
        {
            GError* err = nullptr; gchar* dbg = nullptr;
            gst_message_parse_error(msg, &err, &dbg);
            std::cerr << "[Stream " << self->id_
                      << "] GST ERROR: " << err->message << "\n";
            if (dbg) std::cerr << "[Stream debug] " << dbg << "\n";
            g_error_free(err); g_free(dbg);
            break;
        }
        default: break;
    }
    return TRUE;
}

static void on_negotiation_needed(GstElement*, gpointer user_data)
{
    auto* self = static_cast<WebRTCStreamer*>(user_data);
    std::cout << "[Stream " << self->id_ << "] on-negotiation-needed\n";
    if (!self->offer_created_)
    {
        self->offer_created_ = true;
        post(self->ctx_, cb_create_offer, self);
    }
}

WebRTCStreamer::WebRTCStreamer(
    std::shared_ptr<SignalingClient> sc,
    int id
)
:
    sc_(std::move(sc)),
    id_(id)
{
    std::cout << "[Stream " << id_ << "] Constructor start\n";

    // Use a pipeline that links rtph264pay directly into webrtcbin.
    // webrtcbin will auto-create a transceiver when the pad is linked.
    std::string pipeline_desc =
        "appsrc name=src is-live=true format=time "
        "caps=video/x-raw,format=BGRx,width=1280,height=720,framerate=30/1 "
        "! videoconvert "
        "! video/x-raw,format=I420 "
        "! x264enc tune=zerolatency speed-preset=ultrafast key-int-max=30 "
        "! rtph264pay config-interval=1 pt=96 "
        "! application/x-rtp,media=video,encoding-name=H264,payload=96 "
        "! webrtcbin name=webrtc bundle-policy=max-bundle "
        "  stun-server=stun://stun.l.google.com:19302";

    GError* error = nullptr;
    pipeline_ = gst_parse_launch(pipeline_desc.c_str(), &error);

    if (!pipeline_)
    {
        std::cerr << "[Stream " << id_ << "] Pipeline error: "
                  << (error ? error->message : "unknown") << "\n";
        if (error) g_error_free(error);
        return;
    }

    std::cout << "[Stream " << id_ << "] Pipeline created OK\n";

    appsrc_ = gst_bin_get_by_name(GST_BIN(pipeline_), "src");
    webrtc_ = gst_bin_get_by_name(GST_BIN(pipeline_), "webrtc");

    if (!appsrc_ || !webrtc_)
    {
        std::cerr << "[Stream " << id_ << "] ERROR: missing elements\n";
        return;
    }

    std::cout << "[Stream " << id_ << "] appsrc + webrtc acquired\n";

    g_object_set(appsrc_,
        "is-live", TRUE,
        "format", GST_FORMAT_TIME,
        "do-timestamp", TRUE,
        NULL);

    GstCaps* caps = gst_caps_from_string(
        "video/x-raw,format=BGRx,width=1280,height=720,framerate=30/1"
    );
    g_object_set(appsrc_, "caps", caps, NULL);
    gst_caps_unref(caps);

    g_signal_connect(webrtc_, "on-ice-candidate",
        G_CALLBACK(on_ice_candidate), this);

    g_signal_connect(webrtc_, "on-negotiation-needed",
        G_CALLBACK(on_negotiation_needed), this);

    sc_->register_handler(id_,
        [this](const nlohmann::json& msg) { handle_message(msg); });

    std::cout << "[Stream " << id_ << "] Constructor done\n";
}

void WebRTCStreamer::start_send()
{
    std::cout << "[Stream " << id_ << "] start_send\n";

    ctx_  = g_main_context_new();
    loop_ = g_main_loop_new(ctx_, FALSE);

    GstBus* bus = gst_element_get_bus(pipeline_);
    GSource* bus_src = gst_bus_create_watch(bus);
    gst_object_unref(bus);
    g_source_set_callback(bus_src,
        reinterpret_cast<GSourceFunc>(reinterpret_cast<void*>(cb_bus_watch)),
        this, nullptr);
    g_source_attach(bus_src, ctx_);
    g_source_unref(bus_src);

    std::mutex mtx;
    std::condition_variable cv;
    bool running = false;

    loop_thread_ = std::thread([this, &mtx, &cv, &running]()
    {
        g_main_context_push_thread_default(ctx_);
        { std::lock_guard<std::mutex> lk(mtx); running = true; }
        cv.notify_one();
        std::cout << "[Stream " << id_ << "] GLib loop starting\n";
        g_main_loop_run(loop_);
        std::cout << "[Stream " << id_ << "] GLib loop exited\n";
        g_main_context_pop_thread_default(ctx_);
    });

    { std::unique_lock<std::mutex> lk(mtx);
      cv.wait(lk, [&running]{ return running; }); }

    std::cout << "[Stream " << id_ << "] setting PLAYING\n";
    gst_element_set_state(pipeline_, GST_STATE_PLAYING);

    std::cout << "[Stream " << id_ << "] start_send done\n";
}

void WebRTCStreamer::create_offer()
{
    std::cout << "[Stream " << id_ << "] create_offer emit\n";
    GstPromise* promise = gst_promise_new_with_change_func(
        on_offer_created, this, nullptr);
    g_signal_emit_by_name(webrtc_, "create-offer", nullptr, promise);
}

void WebRTCStreamer::handle_message(const nlohmann::json& msg)
{
    std::string type = msg.value("type", "");

    if (type == "answer")
    {
        GstSDPMessage* sdp = nullptr;
        gst_sdp_message_new(&sdp);
        auto sdp_str = msg["sdp"].get<std::string>();
        gst_sdp_message_parse_buffer(
            reinterpret_cast<const guint8*>(sdp_str.data()),
            sdp_str.size(), sdp);

        auto* answer = gst_webrtc_session_description_new(
            GST_WEBRTC_SDP_TYPE_ANSWER, sdp);

        GstPromise* p = gst_promise_new();
        g_signal_emit_by_name(webrtc_, "set-remote-description", answer, p);
        gst_promise_interrupt(p);
        gst_promise_unref(p);
        gst_webrtc_session_description_free(answer);

        std::cout << "[Stream " << id_ << "] answer set\n";
    }
    else if (type == "ice")
    {
        g_signal_emit_by_name(webrtc_, "add-ice-candidate",
            msg["sdpMLineIndex"].get<int>(),
            msg["candidate"].get<std::string>().c_str());
    }
}

void WebRTCStreamer::on_ice_candidate(
    GstElement*, guint mline, gchar* candidate, gpointer user_data)
{
    auto* self = static_cast<WebRTCStreamer*>(user_data);
    std::cout << "[Stream " << self->id_ << "] ICE candidate\n";

    nlohmann::json msg = {
        {"type", "ice"}, {"candidate", candidate},
        {"sdpMLineIndex", mline}, {"id", self->id_},
        {"target_id", self->id_ + 100}
    };
    self->sc_->send(msg.dump());
}

void WebRTCStreamer::on_offer_created(GstPromise* promise, gpointer user_data)
{
    auto* self = static_cast<WebRTCStreamer*>(user_data);

    const GstStructure* reply = gst_promise_get_reply(promise);
    if (!reply) { gst_promise_unref(promise); return; }

    GstWebRTCSessionDescription* offer = nullptr;
    gst_structure_get(reply, "offer",
        GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &offer, nullptr);

    if (!offer) {
        std::cerr << "[Stream " << self->id_ << "] offer is null\n";
        gst_promise_unref(promise);
        return;
    }

    GstPromise* lp = gst_promise_new();
    g_signal_emit_by_name(self->webrtc_, "set-local-description", offer, lp);
    gst_promise_interrupt(lp);
    gst_promise_unref(lp);

    self->ready_ = true;

    gchar* sdp_text = gst_sdp_message_as_text(offer->sdp);
    std::cout << "[Stream " << self->id_ << "] offer created, sending\n";

    nlohmann::json msg = {
        {"type", "offer"}, {"sdp", sdp_text},
        {"id", self->id_}, {"target_id", self->id_ + 100}
    };
    self->sc_->send(msg.dump());

    g_free(sdp_text);
    gst_webrtc_session_description_free(offer);
    gst_promise_unref(promise);
}

void WebRTCStreamer::push_frame(const cv::Mat& frame)
{
    if (!ready_ || !appsrc_) return;

    static int count = 0;
    if (++count == 1)
        std::cout << "[Stream " << id_ << "] first frame pushed\n";

    const GstClockTime duration = gst_util_uint64_scale_int(1, GST_SECOND, 30);
    const size_t size = frame.total() * frame.elemSize();

    GstBuffer* buffer = gst_buffer_new_allocate(nullptr, size, nullptr);
    GstMapInfo info;
    if (gst_buffer_map(buffer, &info, GST_MAP_WRITE))
    {
        std::memcpy(info.data, frame.data, size);
        gst_buffer_unmap(buffer, &info);
    }

    GST_BUFFER_PTS(buffer) = timestamp_;
    GST_BUFFER_DTS(buffer) = timestamp_;
    GST_BUFFER_DURATION(buffer) = duration;
    timestamp_ += duration;

    GstFlowReturn ret;
    g_signal_emit_by_name(appsrc_, "push-buffer", buffer, &ret);
    if (ret != GST_FLOW_OK)
        std::cerr << "[Stream " << id_ << "] push error: " << ret << "\n";

    gst_buffer_unref(buffer);
}

WebRTCStreamer::~WebRTCStreamer()
{
    std::cout << "[Stream " << id_ << "] destructor\n";

    if (pipeline_)
    {
        gst_element_set_state(pipeline_, GST_STATE_NULL);
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
    }

    if (webrtc_) { gst_object_unref(webrtc_); webrtc_ = nullptr; }
    if (loop_) g_main_loop_quit(loop_);
    if (loop_thread_.joinable()) loop_thread_.join();
    if (loop_) { g_main_loop_unref(loop_); loop_ = nullptr; }
    if (ctx_)  { g_main_context_unref(ctx_); ctx_ = nullptr; }
}

} // namespace routing