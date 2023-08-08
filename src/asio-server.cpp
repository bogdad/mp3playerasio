//
// server.cpp
// ~~~~~~~~~~
//
// Copyright (c) 2003-2023 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <asio.hpp>
#include <asio/basic_streambuf.hpp>
#include <asio/buffer.hpp>
#include <asio/detail/string_view.hpp>
#include <asio/error.hpp>
#include <asio/error_code.hpp>
#include <asio/io_context.hpp>
#include <asio/read.hpp>
#include <asio/read_until.hpp>
#include <asio/steady_timer.hpp>
#include <asio/streambuf.hpp>
#include <atomic>
#include <ctime>
#include <future>
#include <iostream>
#include <memory>
#include <ostream>
#include <string>
#include <string_view>

using asio::ip::tcp;

std::string make_daytime_string() {
  using namespace std; // For time_t, time and ctime;
  time_t now = time(0);
  return ctime(&now);
}

std::atomic_int should_stop = 0;

void my_handler(int s) {
  should_stop = 1;
}

class infinite_timer {
public:
  static constexpr auto interval = asio::chrono::seconds(10);
  infinite_timer(asio::io_context &io_context): timer_(io_context, interval) {start();}
private:
  void start() {
    timer_.expires_at(timer_.expires_at() + interval);
    timer_.async_wait([this](const asio::error_code &error){
      std::cout << "timer! " << make_daytime_string() << std::endl;
      this->start();
    });
  }
  asio::steady_timer timer_; 
};

class tcp_connection : public std::enable_shared_from_this<tcp_connection> {
public:
  static constexpr auto interval = asio::chrono::seconds(5);
  typedef std::shared_ptr<tcp_connection> pointer;

  static pointer create(asio::io_context &io_context) {
    return pointer(new tcp_connection(io_context), [](tcp_connection *conn){
      std::cout << "deleting " << conn << std::endl;
      delete conn;});
  }

  tcp::socket &socket() { return socket_; }

  void start() {
    message_ = make_daytime_string();

    auto ptr = shared_from_this();
    asio::async_write(socket_, asio::buffer(message_),
        [ptr](const asio::error_code &error, size_t bytes_transferred) {
          ptr->handle_write(error, bytes_transferred);
        }
      );
  }

private:
  tcp_connection(asio::io_context &io_context) : socket_(io_context), timer_(io_context), buff_(1024) {}

  void handle_write(const asio::error_code &error,
                    size_t /*bytes_transferred*/) {
    if (!error) {
      std::cout << "staring reading from the client" << std::endl;
      auto self = shared_from_this();
      timer_.expires_from_now(interval);
      timer_.async_wait([self](const asio::error_code &){
        std::cout << "timeout timer fired" << std::endl;
        self->was_timeout_ = true;
        self->socket_.shutdown(asio::socket_base::shutdown_both);
        self->socket_.close();
      });
      asio::async_read_until(socket_, buff_, '\n', [self](const asio::error_code err, size_t read_size) {
        if (err == asio::error::operation_aborted) {
          std::cout << "read from client timeout" << std::endl;
          return;
        }
        using asio::streambuf;
        size_t s = 0;
        streambuf::const_buffers_type bufs = self->buff_.data();
        streambuf::const_buffers_type::const_iterator i = bufs.begin();
        std::cout << "read from client: "; 
        while (i != bufs.end()) {
          asio::const_buffer buf(*i++);
          std::cout << std::string_view(static_cast<const char *>(buf.data()), bufs.size());
          s += buf.size();
        }
        std::cout << std::endl;
        self->was_timeout_ = false;
        self->timer_.cancel();
      });
    }
  }
  tcp::socket socket_;
  std::string message_;
  asio::streambuf buff_;
  char delim_ = '\0';
  asio::steady_timer timer_;
  bool was_timeout_{false};
};

class tcp_server {
public:
  tcp_server(asio::io_context &io_context)
      : io_context_(io_context),
        acceptor_(io_context, tcp::endpoint(tcp::v4(), 8060)) {
    start_accept();
  }

private:
  void start_accept() {
    tcp_connection::pointer new_connection =
        tcp_connection::create(io_context_);
    acceptor_.async_accept(new_connection->socket(),
      [this, new_connection](const asio::error_code &error) {
        this->handle_accept(new_connection, error);
      }
    );
  }

  void handle_accept(tcp_connection::pointer new_connection,
                     const asio::error_code &error) {
    if (!error) {
      new_connection->start();
    }

    start_accept();
  }

  asio::io_context &io_context_;
  tcp::acceptor acceptor_;
};

int main() {
  struct sigaction sigIntHandler;

  sigIntHandler.sa_handler = my_handler;
  sigemptyset(&sigIntHandler.sa_mask);
  sigIntHandler.sa_flags = 0;

  sigaction(SIGINT, &sigIntHandler, NULL);


  try {
    asio::io_context io_context;
    tcp_server server(io_context);
    infinite_timer timer(io_context);
    while(!should_stop) {
      io_context.run_one();
    }
  } catch (std::exception &e) {
    std::cerr << e.what() << std::endl;
  }

  return 0;
}
