#include <chrono>
#include <iostream>
#include <gst/gst.h>
#include <opencv2/opencv.hpp>
#include <string>
#include <thread>
#include <vector>

#include "SignalingClient.hpp"
#include "GStreamerVideoReader.hpp"
#include "WebRTCStreamer.hpp"

void show_stream(std::string path, std::unique_ptr<routing::WebRTCStreamer> stream)
{
    std::cout << "Starting stream for: " << path << std::endl;

    stream->start_send();

    std::cout << "Creating reader..." << std::endl;

    GStreamerVideoReader reader(std::move(path));

    std::cout << "Starting reader..." << std::endl;

    reader.start();

    std::cout << "Reader started." << std::endl;

    int loops = 0;

    while (true)
    {
        ++loops;

        cv::Mat frame;

        bool ok = reader.getFrame(frame);

        if (loops % 200 == 0)
        {
            std::cout << "Loop " << loops
                      << " getFrame=" << ok
                      << " empty=" << frame.empty()
                      << std::endl;
        }

        if (ok)
        {
            std::cout << "Got frame "
                      << frame.cols
                      << "x"
                      << frame.rows
                      << std::endl;

            stream->push_frame(frame);

            //cv::imshow("Video", frame);
        }
        else
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        if (cv::waitKey(1) == 27)
            break;
    }

    reader.stop();
}

int main() {
    gst_init(nullptr, nullptr);

    std::string host = "127.0.0.1";
    std::string port = "8000";
    std::string target = "/ws";

    auto sc = std::make_shared<routing::SignalingClient>(host, port, target);
    sc->connect();

    sc->send_json({{"type", "register"}, {"id", 100}});
    sc->send_json({{"type", "register"}, {"id", 101}});

    std::vector<std::string> paths{
        "/home/honta/Desktop/Embedded_Systems/Networking/WebRTC_GStreamer_Images_To_React/Backend/data/4942181-hd_1280_720_24fps.mp4",
        "/home/honta/Desktop/Embedded_Systems/Networking/WebRTC_GStreamer_Images_To_React/Backend/data/11941725_1280_720_30fps.mp4"
    };

    // Build ALL streamers on the main thread first, serially,
    // so gst_parse_launch / registry access never races
    std::vector<std::unique_ptr<routing::WebRTCStreamer>> streamers;
    for (int i = 0; i < (int)paths.size(); i++)
        streamers.push_back(std::make_unique<routing::WebRTCStreamer>(sc, 100 + i));

    // Now spawn threads — GStreamer registry is already warm, no more races
    std::vector<std::thread> ts;
    for (int i = 0; i < (int)paths.size(); i++)
        ts.emplace_back(show_stream, paths[i], std::move(streamers[i]));

    for (auto& t : ts) t.join();

    return 0;
}