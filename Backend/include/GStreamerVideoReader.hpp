
#pragma once

#include <gst/gst.h>
#include <gst/app/gstappsink.h>

#include <opencv2/opencv.hpp>

#include <mutex>
#include <string>

class GStreamerVideoReader
{
public:
    explicit GStreamerVideoReader(const std::string& filename);
    ~GStreamerVideoReader();

    bool start();
    void stop();

    bool getFrame(cv::Mat& frame);

private:
    static GstFlowReturn onNewSample(GstAppSink* sink, gpointer user_data);

    GstFlowReturn handleNewSample();

private:
    std::string filename_;

    GstElement* pipeline_ = nullptr;
    GstElement* appsink_ = nullptr;

    std::mutex frameMutex_;
    cv::Mat latestFrame_;

    bool running_ = false;
};