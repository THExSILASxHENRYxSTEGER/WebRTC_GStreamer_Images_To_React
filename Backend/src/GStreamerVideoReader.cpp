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
        " ! decodebin "
        "! videoconvert "
        "! video/x-raw,format=BGR "
        "! appsink name=sink";

    GError* error = nullptr;

    pipeline_ = gst_parse_launch(pipelineString.c_str(), &error);

    if (!pipeline_)
    {
        if (error)
        {
            std::cerr << error->message << std::endl;
            g_error_free(error);
        }
        return false;
    }

    appsink_ = gst_bin_get_by_name(GST_BIN(pipeline_), "sink");

    gst_app_sink_set_emit_signals(GST_APP_SINK(appsink_), TRUE);

    GstAppSinkCallbacks callbacks{};
    callbacks.new_sample = onNewSample;

    gst_app_sink_set_callbacks(
        GST_APP_SINK(appsink_),
        &callbacks,
        this,
        nullptr);

    gst_element_set_state(pipeline_, GST_STATE_PLAYING);

    running_ = true;

    return true;
}

void GStreamerVideoReader::stop()
{
    if (!running_)
        return;

    running_ = false;

    gst_element_set_state(pipeline_, GST_STATE_NULL);

    gst_object_unref(appsink_);
    gst_object_unref(pipeline_);

    appsink_ = nullptr;
    pipeline_ = nullptr;
}

bool GStreamerVideoReader::getFrame(cv::Mat& frame)
{
    std::lock_guard<std::mutex> lock(frameMutex_);

    if (latestFrame_.empty())
    {
        static int count = 0;
        if (++count % 1000 == 0)
            std::cout << "getFrame(): latestFrame_ empty\n";

        return false;
    }

    static bool first = true;
    if (first)
    {
        std::cout << "getFrame(): returning first frame\n";
        first = false;
    }

    frame = latestFrame_.clone();

    return true;
}

GstFlowReturn GStreamerVideoReader::onNewSample(
    GstAppSink* sink,
    gpointer user_data)
{
    return static_cast<GStreamerVideoReader*>(user_data)
        ->handleNewSample();
}

GstFlowReturn GStreamerVideoReader::handleNewSample()
{
    static bool first = true;

    if (first)
    {
        std::cout << "FIRST SAMPLE RECEIVED\n";
        first = false;
    }

    GstSample* sample =
        gst_app_sink_pull_sample(GST_APP_SINK(appsink_));

    if (!sample)
        return GST_FLOW_ERROR;

    GstBuffer* buffer = gst_sample_get_buffer(sample);
    GstCaps* caps = gst_sample_get_caps(sample);

    GstStructure* structure =
        gst_caps_get_structure(caps, 0);

    int width, height;

    gst_structure_get_int(structure, "width", &width);
    gst_structure_get_int(structure, "height", &height);

    GstMapInfo map;

    gst_buffer_map(buffer, &map, GST_MAP_READ);

    cv::Mat image(
        height,
        width,
        CV_8UC3,
        map.data);

    {
        std::lock_guard<std::mutex> lock(frameMutex_);
        latestFrame_ = image.clone();
        //std::cout << "Stored frame "
        //        << latestFrame_.cols
        //        << "x"
        //        << latestFrame_.rows
        //        << std::endl;
    }

    gst_buffer_unmap(buffer, &map);

    gst_sample_unref(sample);

    return GST_FLOW_OK;
}