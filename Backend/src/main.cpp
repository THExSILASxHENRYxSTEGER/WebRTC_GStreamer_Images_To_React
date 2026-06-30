#include <chrono>
#include <iostream>
#include <gst/gst.h>
#include <opencv2/opencv.hpp>
#include <string>
#include <thread>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

#include "SignalingClient.hpp"
#include "GStreamerVideoReader.hpp"
#include "WebRTCStreamer.hpp"

// Runs entirely inside a forked child process: one stream, one webrtcbin,
// completely isolated OpenSSL/DTLS state from any other stream.
static void run_stream(int stream_id, const std::string& path)
{
    // gst_init must happen AFTER fork, inside the child, so this process
    // gets its own fresh GStreamer/OpenSSL state with no inherited
    // file descriptors or static state from a sibling process.
    gst_init(nullptr, nullptr);

    std::string host = "127.0.0.1";
    std::string port = "8000";
    std::string target = "/ws";

    auto sc = std::make_shared<routing::SignalingClient>(host, port, target);
    sc->connect();

    sc->send_json({{"type", "register"}, {"id", stream_id}});

    auto stream = std::make_unique<routing::WebRTCStreamer>(sc, stream_id);

    std::cout << "[" << stream_id << "] Starting stream for: " << path << std::endl;
    stream->start_send();

    GStreamerVideoReader reader(path);
    reader.start();

    using clock = std::chrono::steady_clock;
    using ms = std::chrono::milliseconds;
    const auto frame_interval = ms(1000 / 30);
    auto next_frame = clock::now();

    int loops = 0;

    while (true)
    {
        ++loops;

        auto now = clock::now();
        if (now < next_frame)
            std::this_thread::sleep_until(next_frame);
        next_frame += frame_interval;

        cv::Mat frame;
        bool ok = reader.getFrame(frame);

        if (loops % 150 == 0)
            std::cout << "[" << stream_id << "] Loop " << loops << " getFrame=" << ok << "\n";

        if (ok)
            stream->push_frame(frame);
    }

    reader.stop();
}

int main()
{
    struct StreamConfig
    {
        int id;
        std::string path;
    };

    std::vector<StreamConfig> streams = {
        { 100, "/home/honta/Desktop/Embedded_Systems/Networking/WebRTC_GStreamer_Images_To_React/Backend/data/4942181-hd_1280_720_24fps.mp4" },
        { 101, "/home/honta/Desktop/Embedded_Systems/Networking/WebRTC_GStreamer_Images_To_React/Backend/data/11941725_1280_720_30fps.mp4" },
    };

    std::vector<pid_t> child_pids;

    for (const auto& cfg : streams)
    {
        pid_t pid = fork();

        if (pid < 0)
        {
            std::cerr << "fork() failed for stream " << cfg.id << "\n";
            continue;
        }

        if (pid == 0)
        {
            // Child process: run exactly one stream, then exit.
            // No GStreamer/OpenSSL state has been touched yet in this
            // process, so this webrtcbin is fully isolated from any
            // sibling stream's DTLS/SSL context.
            run_stream(cfg.id, cfg.path);
            _exit(0); // never falls through to running other streams
        }

        // Parent: remember the child PID and continue to fork the next one
        child_pids.push_back(pid);
    }

    // Parent process waits for all children to finish (they run forever
    // until killed, e.g. Ctrl+C, which sends SIGINT to the whole process
    // group including children if run normally in a terminal)
    for (pid_t pid : child_pids)
    {
        int status = 0;
        waitpid(pid, &status, 0);
    }

    return 0;
}