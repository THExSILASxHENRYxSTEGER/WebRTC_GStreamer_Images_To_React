#include "GStreamerVideoReader.hpp"
#include <iostream>

GStreamerVideoReader::GStreamerVideoReader(const std::string& filename)
    : filename_(filename)
{}

GStreamerVideoReader::~GStreamerVideoReader()
{
    stop();
}

bool GStreamerVideoReader::start()
{
    std::string pipelineString =
        "filesrc location=" + filename_ +
        " ! decodebin"
        " ! videoconvert"
        " ! video/x-raw,format=BGR"
        " ! appsink name=sink";

    GError* error = nullptr;
    pipeline_ = gst_parse_launch(pipelineString.c_str(), &error);

    if (!pipeline_)
    {
        if (error) { std::cerr << error->message << "\n"; g_error_free(error); }
        return false;
    }

appsink_ = gst_bin_get_by_name(GST_BIN(pipeline_), "sink");

gst_app_sink_set_emit_signals(GST_APP_SINK(appsink_), TRUE);
gst_app_sink_set_max_buffers(GST_APP_SINK(appsink_), 1);  // ADD THIS
gst_app_sink_set_drop(GST_APP_SINK(appsink_), TRUE);       // ADD THIS — drop old frames


    GstAppSinkCallbacks callbacks{};
    callbacks.new_sample = onNewSample;
    gst_app_sink_set_callbacks(GST_APP_SINK(appsink_), &callbacks, this, nullptr);

    gst_element_set_state(pipeline_, GST_STATE_PLAYING);
    running_ = true;
    return true;
}

void GStreamerVideoReader::stop()
{
    if (!running_) return;
    running_ = false;
    gst_element_set_state(pipeline_, GST_STATE_NULL);
    gst_object_unref(appsink_);
    gst_object_unref(pipeline_);
    appsink_ = nullptr;
    pipeline_ = nullptr;
}

bool GStreamerVideoReader::getFrame(cv::Mat& frame)
{
    cv::Mat bgr;
    {
        std::lock_guard<std::mutex> lock(frameMutex_);
        if (latestFrame_.empty()) return false;
        bgr = std::move(latestFrame_);
        latestFrame_ = cv::Mat{};
    }
    // Convert BGR -> BGRx outside the lock
    cv::cvtColor(bgr, frame, cv::COLOR_BGR2BGRA);
    return true;
}

GstFlowReturn GStreamerVideoReader::onNewSample(GstAppSink* sink, gpointer user_data)
{
    return static_cast<GStreamerVideoReader*>(user_data)->handleNewSample();
}

GstFlowReturn GStreamerVideoReader::handleNewSample()
{
    static bool first = true;
    if (first) { std::cout << "FIRST SAMPLE RECEIVED\n"; first = false; }

    GstSample* sample = gst_app_sink_pull_sample(GST_APP_SINK(appsink_));
    if (!sample) return GST_FLOW_ERROR;

    GstBuffer* buffer = gst_sample_get_buffer(sample);
    GstCaps*   caps   = gst_sample_get_caps(sample);
    GstStructure* structure = gst_caps_get_structure(caps, 0);

    int width = 0, height = 0;
    gst_structure_get_int(structure, "width",  &width);
    gst_structure_get_int(structure, "height", &height);

    GstMapInfo map;
    gst_buffer_map(buffer, &map, GST_MAP_READ);

    const size_t expected = (size_t)width * height * 3;
    if (map.size >= expected && width > 0 && height > 0)
    {
        // Store as BGR — conversion happens in getFrame
        cv::Mat bgr(height, width, CV_8UC3, map.data);

        std::lock_guard<std::mutex> lock(frameMutex_);
        latestFrame_ = bgr.clone();  // clone while mapped
    }

    gst_buffer_unmap(buffer, &map);
    gst_sample_unref(sample);
    return GST_FLOW_OK;
}