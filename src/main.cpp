#include "headers.h"

#include <boost/asio.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <string_view>
#include <iostream>

using boost::asio::io_service;
using boost::asio::co_spawn;
using boost::asio::async_read_until;
using boost::asio::awaitable;
using boost::asio::use_awaitable;
using boost::system::error_code;
using boost::asio::buffer;
using boost::asio::dynamic_buffer;
using boost::asio::transfer_at_least;
using boost::asio::ip::tcp;


constexpr std::string_view delimiter = "\r\n\r\n";


awaitable<void> session(tcp::socket client_socket, io_service& io_service)
{
  // code here
}

class Server
{
public:
  Server(io_service& io_service, short port)
    : io_service_(io_service)
    , acceptor_(io_service, tcp::endpoint(tcp::v4(), port))
    , socket_(io_service)
  {
    do_accept();
  }

private:
  void do_accept()
  {
    acceptor_.async_accept(socket_,
      [this](error_code ec)
      {
        // code here
      }
    );
  }

  io_service& io_service_;
  tcp::acceptor acceptor_;
  tcp::socket socket_;
};

int main(int argc, char* argv[]) {
  try {
    if (argc != 2) {
      std::cerr << "Usage: proxy_server";
      std::cerr << " <listen_port>\n";
      return 1;
    }
    io_service io_service(1);
    Server server(io_service, std::atoi(argv[1]));
    io_service.run();

  } catch (const std::exception& e) {
    std::cerr << "Exception: " << e.what() << std::endl;
  }
}
