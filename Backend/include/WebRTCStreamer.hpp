#pragma once

#include <condition_variable>
#include <mutex>
#include <thread>

#include <gst/gst.h>
#include <gst/webrtc/webrtc.h>
#include <memory>
#include <opencv2/opencv.hpp>

#include "SignalingClient.hpp"

namespace routing {

    class WebRTCStreamer
    {
        public:
            
            WebRTCStreamer(std::shared_ptr<SignalingClient> websocket, int id);
            ~WebRTCStreamer();

            // public so free static callbacks in .cpp can access them
            GstElement*   webrtc_           = nullptr;
            GstElement*   pipeline_         = nullptr;
            GMainContext* ctx_              = nullptr;
            int           id_              = 0;
            bool          transceiver_added_ = false;
            
            void start_send();
            void create_offer();
            void handle_message(const nlohmann::json& doc);
            void push_frame(const cv::Mat& frame);

        private:
            GstClockTime timestamp_ = 0;

            std::shared_ptr<SignalingClient> sc_;
            std::thread loop_thread_;

            GstElement* appsrc_ = nullptr;
            GMainLoop*  loop_   = nullptr;

            static void on_ice_candidate(GstElement*, guint, gchar*, gpointer);
            static void on_offer_created(GstPromise*, gpointer);
    };

}