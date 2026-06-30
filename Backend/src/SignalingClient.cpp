#include "SignalingClient.hpp"
#include <iostream>

namespace routing {

using tcp = boost::asio::ip::tcp;

SignalingClient::SignalingClient(std::string host,
                                 std::string port,
                                 std::string target)
    : host_(std::move(host))
    , port_(std::move(port))
    , target_(std::move(target))
    , strand_(boost::asio::make_strand(ioc_))
    , ws_(strand_)
{}

SignalingClient::~SignalingClient()
{
    boost::asio::post(strand_, [this]() {
        boost::system::error_code ec;
        ws_.close(boost::beast::websocket::close_code::normal, ec);
    });

    ioc_.stop();

    if (io_thread_.joinable())
        io_thread_.join();
}

void SignalingClient::connect()
{
    tcp::resolver resolver(ioc_);
    auto const results = resolver.resolve(host_, port_);

    boost::asio::connect(ws_.next_layer(), results);

    ws_.handshake(host_, target_);
    ws_.binary(false);
    ws_.auto_fragment(true);

    io_thread_ = std::thread([this]() {
        run_io();
    });

    do_read();
}

void SignalingClient::run_io()
{
    ioc_.run();
}

void SignalingClient::register_handler(int peerId, MessageHandler handler)
{
    std::lock_guard<std::mutex> lock(handlers_mutex_);
    handlers_[peerId] = std::move(handler);
}

void SignalingClient::send(const std::string& message)
{
    boost::asio::post(strand_, [this, message]() {
        write_queue_.push(message);

        if (write_queue_.size() == 1)
            do_write();
    });
}

void SignalingClient::send_json(const nlohmann::json& j)
{
    send(j.dump());
}

void SignalingClient::do_write()
{
    ws_.async_write(
        boost::asio::buffer(write_queue_.front()),
        boost::asio::bind_executor(
            strand_,
            [this](boost::beast::error_code ec, std::size_t)
            {
                if (ec) return;

                write_queue_.pop();

                if (!write_queue_.empty())
                    do_write();
            }
        )
    );
}

void SignalingClient::do_read()
{
    ws_.async_read(
        buffer_,
        boost::asio::bind_executor(
            strand_,
            [this](boost::beast::error_code ec, std::size_t)
            {
                if (ec) return;

                auto text =
                    boost::beast::buffers_to_string(buffer_.data());
                buffer_.consume(buffer_.size());

                try {
                    auto json = nlohmann::json::parse(text);

                    if (json.contains("id")) {
                        int id = json.at("id").get<int>();

                        std::lock_guard<std::mutex> lock(handlers_mutex_);
                        auto it = handlers_.find(id);
                        if (it != handlers_.end())
                            it->second(json);
                    }
                }
                catch (const std::exception& e) {
                    std::cerr << "parse error: " << e.what() << "\n";
                }

                do_read();
            }
        )
    );
}

} // namespace routing