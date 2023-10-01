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
#include <asio.hpp>
#include <asio/basic_streambuf.hpp>
#include <asio/buffer.hpp>
#include <asio/detail/string_view.hpp>
#include <asio/error.hpp>
#include <asio/error_code.hpp>
#include <asio/io_context.hpp>
#include <asio/read.hpp>
#include <asio/read_until.hpp>
#include <asio/signal_set.hpp>
#include <asio/steady_timer.hpp>
#include <asio/streambuf.hpp>
#include <atomic>
#include <ctime>
#include <filesystem>
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

static std::string make_daytime_string() {
  using namespace std; // For time_t, time and ctime;
  time_t now = time(nullptr);
  return ctime(&now);
}

class TcpConnection : public std::enable_shared_from_this<TcpConnection> {
public:
  static constexpr auto interval = asio::chrono::seconds(5);
  using pointer = std::shared_ptr<TcpConnection>;

  static pointer create(asio::io_context &io_context) {
    LOG(INFO) << "creating file";
    Mp3 file = Mp3::create(fs::path("./classical-triumphant-march-163852.mp3"));

    return {new TcpConnection(io_context, std::move(file)),
            [](TcpConnection *conn) {
              delete conn;
            }};
  }

  tcp::socket &socket() { return _socket; }

  void start() {
    asio::error_code ec;
    send_date();
  }

private:
  TcpConnection(asio::io_context &io_context, Mp3 &&file)
      : _socket(io_context), _timer(io_context), _buff(1024),
        _file(std::move(file)),
        _server_decoder(
            [this](buffers_2<std::string_view> msg) { on_message(msg); }) {}

  void send_date() {
    _message = make_daytime_string();

    auto ptr = shared_from_this();
    _server_encoder.fill_time(_message, _write_buffer);

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
    if (_file.send_chunk(_socket) == 0) {
      if (_file.is_all_sent()) {
        asio::error_code ec{};
        // all done
        _socket.close();
      } else {
        auto ptr = shared_from_this();
        _socket.async_write_some(
            asio::null_buffers(),
            [ptr](const asio::error_code &error, size_t bytes_transferred) {
              ptr->send_mp3_inner();
            });
      }
    } else {
      LOG(ERROR) << "sendfile failed";
      _socket.shutdown(asio::socket_base::shutdown_both);
      _socket.close();
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
    _socket.async_write_some(
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

  tcp::socket _socket;
  std::string _message;
  asio::streambuf _buff;
  char _delim = '\0';
  asio::steady_timer _timer;
  bool _was_timeout{false};

  Mp3 _file;
  RingBuffer _write_buffer{8388608};
  ServerEncoder _server_encoder{};
  RingBuffer _read_buffer{8388608};
  ServerDecoder _server_decoder;
  DestructionSignaller _destruction_signaller {"TcpConnection"};
};

class TcpServer {
public:
  TcpServer(asio::io_context &io_context)
      : _io_context(io_context),
        _acceptor(io_context, tcp::endpoint(tcp::v4(), 8060)) {
    start_accept();
  }

private:
  void start_accept() {
    TcpConnection::pointer new_connection =
        TcpConnection::create(_io_context);
    _acceptor.async_accept(
        new_connection->socket(),
        [this, new_connection](const asio::error_code &error) {
          this->handle_accept(new_connection, error);
        });
  }

  void handle_accept(TcpConnection::pointer new_connection,
                     const asio::error_code &error) {
    if (!error) {
      new_connection->start();
    }

    start_accept();
  }

  asio::io_context &_io_context;
  tcp::acceptor _acceptor;
};

} // namespace am

int main() {
  using namespace am;

  std::atomic_int should_stop = 0;
  try {
    asio::io_context io_context;
    asio::signal_set signals{io_context, SIGINT};
    signals.async_wait( [&should_stop](const asio::error_code ec, int signal){
      should_stop = 1;
    });
    TcpServer server(io_context);
    const infinite_timer timer(io_context);
    while (!should_stop) {
      LOG(INFO) << "run one";
      io_context.run_one();
    }
  } catch (std::exception &e) {
    std::cerr << e.what() << std::endl;
  }

  return 0;
}
