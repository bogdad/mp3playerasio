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
    Mp3 file = Mp3::create(fs::path("../inside-you-162760.mp3"));

    return {new TcpConnection(io_context, std::move(file)),
            [](TcpConnection *conn) { delete conn; }};
  }

  tcp::socket &socket() { return _socket; }

  void start() {
    send_date();
  }

  void cancel() {
    _file.cancel();
    //_socket.cancel();
  }

private:
  TcpConnection(asio::io_context &io_context, Mp3 &&file)
      : io_context_(io_context)
      , _socket(io_context)
      , _timer(io_context)
      , _buff(1024)
      , _file(std::move(file))
      , _server_decoder(
            [this](buffers_2<std::string_view> msg) { on_message(msg); }) {}

  void send_date() {
    auto message = make_daytime_string();

    auto ptr = shared_from_this();
    _server_encoder.fill_time(message, _write_buffer);

    send([ptr]() { ptr->send_mp3(); },
         [](const asio::error_code &) { LOG(ERROR) << "send date error"; });
  }

  void send_mp3() {
    auto ptr = shared_from_this();
    _server_encoder.fill_mp3(_file, _write_buffer);
    send(
        [ptr]() {
          LOG(INFO) << "server: sending mp3 envelope success";
          ptr->send_mp3_inner(ptr);
        },
        [ptr](const asio::error_code &ec) {
          LOG(ERROR) << "sending mp3 failed " << ec;
        });
  }
  void send_mp3_inner(const TcpConnection::pointer &ptr) {
    LOG(INFO) << "server: calling sendfile";
    if (_file.send(io_context_, _socket,
                   [ptr](std::size_t left, SendFile &inprogress) {
                     if (left > 0) {
                        LOG(INFO) << "sendfile: inprogress";
                        inprogress.call();
                     } else {
                        LOG(INFO) << "sendfile: closing cancelling";
                        ptr->_socket.close();
                        ptr->_file.cancel();
                     }
                   })) {
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
          if (ec == asio::error::operation_aborted) {
            return;
          }
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

  asio::io_context &io_context_;
  tcp::socket _socket;
  asio::streambuf _buff;
  char _delim = '\0';
  asio::steady_timer _timer;
  bool _was_timeout{false};

  Mp3 _file;
  RingBuffer _write_buffer{8388608, 20000, 40000};
  ServerEncoder _server_encoder{};
  RingBuffer _read_buffer{8388608, 20000, 40000};
  ServerDecoder _server_decoder;
  DestructionSignaller _destruction_signaller{"TcpConnection"};
};

class TcpServer {
public:
  TcpServer(asio::io_context &io_context)
      : io_context_(io_context)
      , acceptor_(io_context, tcp::endpoint(tcp::v4(), 8060)) {
    start_accept();
  }
  void cancel() {
    acceptor_.cancel();
    //for (auto &weak_conn : connections_) {
    //  if (auto conn = weak_conn.lock()) {
    //    conn->cancel();
    //  }
    //}
  }

private:
  void start_accept() {
    LOG(INFO) << "start accept";
    TcpConnection::pointer new_connection = TcpConnection::create(io_context_);
    acceptor_.async_accept(
        new_connection->socket(),
        [this, new_connection](const asio::error_code &error) {
          LOG(INFO) << "accepted " << error;
          if (error == asio::error::operation_aborted) {
            LOG(INFO) << "accepted aborted";
            return;
          }
          connections_.emplace_back(new_connection);
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

  asio::io_context &io_context_;
  tcp::acceptor acceptor_;
  std::vector<std::weak_ptr<TcpConnection>> connections_;
  DestructionSignaller signaller_{"TcpServer"};
};

} // namespace am

int main() {
  using namespace am;

  try {
    asio::io_context io_context;
    asio::io_context::strand strand{io_context};
    asio::signal_set signals{io_context, SIGINT};
    TcpServer server(io_context);
    signals.async_wait(
        [&server, &strand](const asio::error_code ec, int signal) {
          server.cancel();
        });
    io_context.run();
    LOG(INFO)<<"stopping";
  } catch (std::exception &e) {
    std::cerr << e.what() << std::endl;
  }

  return 0;
}
