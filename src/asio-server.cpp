//
// server.cpp
// ~~~~~~~~~~
//
// Copyright (c) 2003-2023 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <absl/base/call_once.h>
#include <absl/base/casts.h>
#include <absl/base/thread_annotations.h>
#include <absl/log/log.h>
#include <absl/strings/str_format.h>
#include <algorithm>
#include <array>
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
#include <filesystem>
#include <future>
#include <iostream>
#include <memory>
#include <ostream>
#include <string>
#include <string_view>
#include <sys/socket.h>

#include "mp3.hpp"
#include "protocol.hpp"
#include "server-protocol.hpp"

using asio::ip::tcp;

namespace am {

std::string make_daytime_string() {
  using namespace std; // For time_t, time and ctime;
  time_t now = time(nullptr);
  return ctime(&now);
}

std::atomic_int should_stop = 0;

void my_handler(int s) { should_stop = 1; }

class infinite_timer {
public:
  static constexpr auto interval = asio::chrono::seconds(10);
  infinite_timer(asio::io_context &io_context) : timer_(io_context, interval) {
    start();
  }

private:
  void start() {
    timer_.expires_at(timer_.expires_at() + interval);
    timer_.async_wait([this](const asio::error_code &error) {
      std::cout << "timer! " << make_daytime_string() << std::endl;
      this->start();
    });
  }
  asio::steady_timer timer_;
};

class tcp_connection : public std::enable_shared_from_this<tcp_connection> {
public:
  static constexpr auto interval = asio::chrono::seconds(5);
  using pointer = std::shared_ptr<tcp_connection>;

  static pointer create(asio::io_context &io_context) {
    LOG(INFO) << "creating file";
    Mp3 file = Mp3::create(fs::path("./classical-triumphant-march-163852.mp3"));

    return {new tcp_connection(io_context, std::move(file)),
            [](tcp_connection *conn) {
              std::cout << "deleting " << conn << std::endl;
              delete conn;
            }};
  }

  tcp::socket &socket() { return socket_; }

  void start() {
    asio::error_code ec;
    send_date();
  }

private:
  tcp_connection(asio::io_context &io_context, Mp3 &&file)
      : socket_(io_context), timer_(io_context), buff_(1024),
        _file(std::move(file)),
        _server_decoder(
            [this](buffers_2<std::string_view> msg) { on_message(msg); }) {}

  void send_date() {
    message_ = make_daytime_string();

    auto ptr = shared_from_this();
    _server_encoder.fill_time(message_, _write_buffer);

    send([ptr]() { ptr->send_mp3(); },
         [](const asio::error_code &) { LOG(ERROR) << "send date error"; });
  }

  void send_mp3() {
    auto ptr = shared_from_this();
    _server_encoder.fill_mp3(_file, _write_buffer);
    send(
        [ptr]() {
          LOG(INFO) << "server: sending mp3 envelope success";
          ptr->send_mp3_inner();
        },
        [ptr](const asio::error_code &ec) {
          LOG(ERROR) << "sending mp3 failed " << ec;
        });
  }
  void send_mp3_inner() {
    LOG(INFO) << "server: calling sendfile";
    if (_file.send_chunk(socket_) == 0) {
      if (_file.is_all_sent()) {
        asio::error_code ec{};
        // all done
        socket_.close();
      } else {
        auto ptr = shared_from_this();
        socket_.async_write_some(
            asio::null_buffers(),
            [ptr](const asio::error_code &error, size_t bytes_transferred) {
              ptr->send_mp3_inner();
            });
      }
    } else {
      LOG(ERROR) << "sendfile failed";
      socket_.shutdown(asio::socket_base::shutdown_both);
      socket_.close();
    };
  }

  void on_message(buffers_2<std::string_view> msg) {
    for (auto part : msg) {
      LOG(INFO) << "clent sent " << part;
    }
  }

  void
  send(absl::AnyInvocable<void() const> &&continuation,
       absl::AnyInvocable<void(const asio::error_code &) const> &&on_error) {
    auto ptr = shared_from_this();
    socket_.async_write_some(
        _write_buffer.data(), [this, ptr, on_error = std::move(on_error),
                               continuation = std::move(continuation)](
                                  const asio::error_code &ec,
                                  const size_t bytes_transferred) mutable {
          LOG(INFO) << "server: sending send" << _write_buffer;
          if (ec) {
            _write_buffer.commit(bytes_transferred);
            on_error(ec);
          } else {
            _write_buffer.commit(bytes_transferred);
            if (_write_buffer.empty()) {
              _write_buffer.reset();
              continuation();
            } else {
              send(std::move(continuation), std::move(on_error));
            }
          }
        });
  }

  tcp::socket socket_;
  std::string message_;
  asio::streambuf buff_;
  char delim_ = '\0';
  asio::steady_timer timer_;
  bool was_timeout_{false};

  Mp3 _file;
  RingBuffer _write_buffer{8388608};
  ServerEncoder _server_encoder{};
  RingBuffer _read_buffer{8388608};
  ServerDecoder _server_decoder;
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
    acceptor_.async_accept(
        new_connection->socket(),
        [this, new_connection](const asio::error_code &error) {
          this->handle_accept(new_connection, error);
        });
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

} // namespace am

int main() {
  using namespace am;
  struct sigaction sigIntHandler {};

  sigIntHandler.sa_handler = my_handler;
  sigemptyset(&sigIntHandler.sa_mask);
  sigIntHandler.sa_flags = 0;

  sigaction(SIGINT, &sigIntHandler, nullptr);

  try {
    asio::io_context io_context;
    tcp_server server(io_context);
    const infinite_timer timer(io_context);
    while (!should_stop) {
      io_context.run_one();
    }
  } catch (std::exception &e) {
    std::cerr << e.what() << std::endl;
  }

  return 0;
}
