#include "headers.h"

#include <atomic>
#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/completion_condition.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <cstddef>
#include <iostream>
#include <memory>
#include <optional>
#include <print>
#include <string_view>

using boost::asio::async_read_until;
using boost::asio::awaitable;
using boost::asio::buffer;
using boost::asio::co_spawn;
using boost::asio::dynamic_buffer;
using boost::asio::io_service;
using boost::asio::transfer_at_least;
using boost::asio::use_awaitable;
using boost::asio::ip::tcp;
using boost::system::error_code;

constexpr std::string_view delimiter = "\r\n\r\n";
constexpr auto forward_size = 8192;

class SocketHandle {
    static size_t socket_handle_id_counter_;

public:
    enum class TypeTag { Client, Server };
    explicit SocketHandle(tcp::socket &&sock, TypeTag tg = TypeTag::Client) : socket_(std::move(sock)), type_tag_(tg) {}
    ~SocketHandle() {
        if (!is_shutdown_.load(std::memory_order_acquire)) {
            shutdown();
        }
    }

    void shutdown() {
        auto already_shutdowning = is_shutdown_.exchange(true, std::memory_order_release);
        if (already_shutdowning) {
            return;
        }
        boost::system::error_code ec;
        auto _ = socket_.shutdown(tcp::socket::shutdown_both, ec);
        auto __ = socket_.close(ec);
        std::println("[{}-{}] Shutdown", get_type_str(), socket_handle_id_);
    }

    tcp::socket &get_socket() { return socket_; }
    size_t get_id() const { return socket_handle_id_; }
    std::string get_type_str() const { return type_tag_ == TypeTag::Client ? "Client" : "Server"; }
    std::string get_info_str() const { return std::format("[{}-{}]", get_type_str(), socket_handle_id_); }

private:
    tcp::socket socket_;
    size_t socket_handle_id_ = socket_handle_id_counter_++;
    std::atomic<bool> is_shutdown_ = false;
    TypeTag type_tag_;
};

size_t SocketHandle::socket_handle_id_counter_ = 0;

awaitable<size_t> read_headers_part(std::shared_ptr<SocketHandle> &sock_handle, boost::asio::streambuf &buffer,
                                    boost::asio::cancellation_slot slot);

awaitable<std::optional<tcp::socket>> resolve_remote_host(io_service &loop, std::string host, std::string port,
                                                          boost::asio::cancellation_slot slot);

bool is_cancellation_exception(const std::exception &e);

awaitable<void> session(std::shared_ptr<SocketHandle> client_sock_handle, io_service &io_service,
                        boost::asio::cancellation_slot slot) {

    boost::asio::cancellation_state cancel_state(slot, boost::asio::enable_total_cancellation());
    auto stop_binder = boost::asio::bind_cancellation_slot(slot, use_awaitable);
    boost::asio::streambuf buffer;

    try {
        for (;;) {
            // here I wait for client request to get host/port
            auto req_header_bytes = co_await read_headers_part(client_sock_handle, buffer, slot);

            std::string req_str;
            req_str.reserve(req_header_bytes);
            {
                auto bufs = buffer.data();
                req_str.assign(boost::asio::buffers_begin(bufs),
                               boost::asio::buffers_begin(bufs) + static_cast<std::ptrdiff_t>(req_header_bytes));
            }

            auto val = findHostPort(req_str);
            if (!val) {
                std::print(std::cerr, "{} Invalid or missing host header found in request\n",
                           client_sock_handle->get_info_str());
                co_return;
            }
            auto &host = val->first;
            auto &port = val->second;

            // client -> server: resolve/connect server
            auto server_socket = co_await resolve_remote_host(io_service, host, port, slot);
            if (!server_socket) {
                std::print(std::cerr, "[{}] Could not connect to {}:{}\n", client_sock_handle->get_id(), host, port);
                co_return;
            }
            auto server_socket_handle =
                std::make_shared<SocketHandle>(std::move(*server_socket), SocketHandle::TypeTag::Server);

            // client -> server: forward whatever is already in buffer (headers + whatever is left in the buffer)
            try {
                co_await boost::asio::async_write(server_socket_handle->get_socket(), buffer.data(), stop_binder);
            } catch (boost::system::system_error const &e) {
                std::print(std::cout, "{} Write to server failed: {}\n", client_sock_handle->get_info_str(), e.what());
                co_return;
            }
            buffer.consume(buffer.size());

            // server -> client: read response headers from server
            std::size_t resp_header_bytes = co_await read_headers_part(server_socket_handle, buffer, slot);

            std::string resp_str;
            resp_str.reserve(resp_header_bytes);
            {
                auto bufs = buffer.data();
                resp_str.assign(boost::asio::buffers_begin(bufs),
                                boost::asio::buffers_begin(bufs) + static_cast<std::ptrdiff_t>(resp_header_bytes));
            }

            // find content length if present
            auto content_length_exp = findContentLength(resp_str);
            if (!content_length_exp) {
                std::print(std::cerr, "{} Server responded with invalid Content-Length header\n",
                           server_socket_handle->get_info_str());
                co_return;
            }
            auto content_length_opt = *content_length_exp;

            if (!content_length_opt || *content_length_opt == 0) {
                // No Content-Length -> then it's 2 options:
                // 1.Transfer-Encoding: chunked, need to read chunk by chunk,
                // 2.or server will close connection to signal end-of-body.
                try {
                    // forward whatever we have now
                    co_await boost::asio::async_write(client_sock_handle->get_socket(), buffer.data(), stop_binder);
                    buffer.consume(buffer.size());

                    // now forward until server closes (read some / write some)
                    for (;;) {
                        std::array<char, forward_size> temp;
                        std::size_t n = co_await server_socket_handle->get_socket().async_read_some(
                            boost::asio::buffer(temp), stop_binder);
                        if (n == 0)
                            break;
                        co_await boost::asio::async_write(client_sock_handle->get_socket(),
                                                          boost::asio::buffer(temp.data(), n), stop_binder);
                    }
                } catch (boost::system::system_error const &e) {
                    if (e.code() != boost::asio::error::eof) {
                        std::print(std::cout, "{} streaming (no content-length) failed: {}\n",
                                   client_sock_handle->get_info_str(), e.what());
                    }
                    // EOF or other -> close session
                }
                co_return;
            }

            size_t content_length = *content_length_opt;
            // content-length present => calculate how many body bytes already in buffer (after header)
            std::size_t body_already = buffer.size() - resp_header_bytes;
            if (content_length > body_already) {
                std::size_t remaining = content_length - body_already;
                // read exactly the remaining bytes
                try {
                    co_await boost::asio::async_read(server_socket_handle->get_socket(), buffer,
                                                     boost::asio::transfer_exactly(remaining), stop_binder);
                } catch (boost::system::system_error const &e) {
                    std::print(std::cout, "{} Failed to read remaining response body: {}\n",
                               client_sock_handle->get_info_str(), e.what());
                    co_return;
                }
            }
            // now buffer contains header + full body; forward to client
            try {
                co_await boost::asio::async_write(client_sock_handle->get_socket(), buffer.data(), stop_binder);
            } catch (boost::system::system_error const &e) {
                std::print(std::cout, "{} Write to client failed: {}\n", client_sock_handle->get_info_str(), e.what());
                co_return;
            }
            buffer.consume(buffer.size());
        }
    } catch (boost::system::system_error const &e) {
        std::print(std::cout, "{} Session terminated due to exception: {}\n", client_sock_handle->get_info_str(),
                   e.what());
    }
}

awaitable<size_t> read_headers_part(std::shared_ptr<SocketHandle> &sock_handle, boost::asio::streambuf &buffer,
                                    boost::asio::cancellation_slot slot) {
    auto stop_binder = boost::asio::bind_cancellation_slot(slot, use_awaitable);
    try {
        co_return co_await async_read_until(sock_handle->get_socket(), buffer, delimiter, stop_binder);
    } catch (boost::system::system_error const &e) {
        if (is_cancellation_exception(e)) {
            std::print(std::cout, "[{}] Read headers cancelled: {}\n", sock_handle->get_id(), e.what());

        } else {
            std::print(std::cerr, "[{}] Read headers error: {}\n", sock_handle->get_id(), e.what());
        }
        throw;
    } catch (std::exception &e) {
        std::print(std::cerr, "[{}] Read headers error: {}\n", sock_handle->get_id(), e.what());
        throw;
    }
}

awaitable<std::optional<tcp::socket>> resolve_remote_host(io_service &loop, std::string host, std::string port,
                                                          boost::asio::cancellation_slot slot) {
    auto stop_binder = boost::asio::bind_cancellation_slot(slot, use_awaitable);
    tcp::resolver resolver(loop);
    tcp::socket server_socket(loop);
    try {
        auto endpoints = co_await resolver.async_resolve(host, port, stop_binder);
        co_await boost::asio::async_connect(server_socket, endpoints, stop_binder);
        co_return server_socket;
    } catch (boost::system::system_error const &e) {
        if (is_cancellation_exception(e)) {
            std::print(std::cout, "Resolve/connect to {}:{} cancelled: {}\n", host, port, e.what());
            co_return std::nullopt;
        } else {
            std::print(std::cerr, "Resolve/connect to {}:{} failed: {}\n", host, port, e.what());
            co_return std::nullopt;
        }
        throw;
    }
}

bool is_cancellation_exception(const std::exception &e) {
    try {
        std::rethrow_if_nested(e);
    } catch (const boost::system::system_error &se) {
        if (se.code() == boost::asio::error::operation_aborted) {
            return true;
        }
    } catch (...) {
    }
    return false;
}

class Server {
public:
    Server(io_service &io_service, short port)
        : io_service_(io_service), strand_(boost::asio::make_strand(io_service)),
          acceptor_(io_service, tcp::endpoint(tcp::v4(), port)), socket_(io_service) {
        do_accept();
    }

    void shutdown() {
        boost::system::error_code ec;

        auto val = acceptor_.close(ec);

        cancel_.emit(boost::asio::cancellation_type::all);
    }

private:
    void do_accept() {
        acceptor_.async_accept(socket_, boost::asio::bind_executor(strand_, [this](error_code ec) {
                                   if (ec) {
                                       std::print(std::cerr, "Accept error: {}\n", ec.message());
                                       return;
                                   }
                                   auto sock_handle_ptr = std::make_shared<SocketHandle>(std::move(socket_));
                                   co_spawn(io_service_,
                                            session(std::move(sock_handle_ptr), io_service_, cancel_.slot()),
                                            boost::asio::detached);
                                   do_accept();
                               }));
    }

    io_service &io_service_;
    boost::asio::strand<io_service::executor_type> strand_;
    boost::asio::cancellation_signal cancel_;

    tcp::acceptor acceptor_;
    tcp::socket socket_;
};

int main(int argc, char *argv[]) {
    try {
        if (argc != 2) {
            std::cerr << "Usage: proxy_server";
            std::cerr << " <listen_port>\n";
            return 1;
        }
        io_service io_service(1);

        boost::asio::signal_set signals(io_service, SIGINT, SIGTERM);

        Server server(io_service, std::atoi(argv[1]));

        signals.async_wait([&](auto, auto) { server.shutdown(); });

        io_service.run();

    } catch (const std::exception &e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }
}
