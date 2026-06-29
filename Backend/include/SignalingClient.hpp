#pragma once

#include <boost/beast.hpp>
#include <boost/asio.hpp>
#include <nlohmann/json.hpp>

#include <functional>
#include <thread>
#include <map>
#include <mutex>
#include <queue>
#include <string>
#include <unordered_map>

namespace routing {

    class SignalingClient
    {
        public:
            using MessageHandler = std::function<void(const nlohmann::json&)>;

            SignalingClient(std::string host, std::string port, std::string target);

            ~SignalingClient();

            void connect();

            void send(const std::string& message);
            void send_json(const nlohmann::json& j);

            void register_handler(int peerId, MessageHandler handler);


        
        private:
            void do_read();
            void do_write();

            void run_io();

        private:
            std::string host_;
            std::string port_;
            std::string target_;

            boost::asio::io_context ioc_;
            boost::beast::websocket::stream<boost::asio::ip::tcp::socket> ws_;

            boost::beast::flat_buffer buffer_;

            std::thread io_thread_;

            std::mutex write_mutex_;
            std::queue<std::string> write_queue_;

            std::unordered_map<int, MessageHandler> handlers_;

            std::mutex handlers_mutex_;
    };

} // namespace routing