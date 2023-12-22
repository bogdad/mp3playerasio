#pragma once

#include "audio-player.hpp"
#include "client-protocol.hpp"
#include "protocol.hpp"

#include <absl/functional/any_invocable.h>
#include <asio.hpp>
#include <asio/ip/tcp.hpp>
#include <memory>

namespace asio {
struct io_context;
} // namespace asio

namespace am {

struct TcpClientConnection : std::enable_shared_from_this<TcpClientConnection> {

  using Pointer = std::shared_ptr<TcpClientConnection>;

  static TcpClientConnection::Pointer create(asio::io_context &io_context,
                                             asio::io_context::strand &strand,
                                             Mp3Stream &mp3_stream);

  void on_connect();

  asio::ip::tcp::socket &socket();

private:
  TcpClientConnection(asio::io_context &io_context,
                      asio::io_context::strand &strand, Mp3Stream &mp3_stream);

  void
  receive(std::function<void(const asio::error_code &)> &&on_error);

  void handle();
  asio::io_context::strand &strand_;
  asio::ip::tcp::socket _socket;
  // FIX lifetime
  Mp3Stream &mp3_stream_;
  ClientEncoder _client_encoder{};
  ClientDecoder _client_decoder;
  DestructionSignaller _destruction_signaller{"TcpClientConnection"};
};

struct AsioClient {
  AsioClient(asio::io_context &io_context, asio::io_context::strand &strand,
             Mp3Stream &mp3_stream);
  void connect(std::string_view host);

private:
  asio::io_context &io_context_;
  asio::io_context::strand &strand_;
  Mp3Stream &mp3_stream_;
  asio::ip::tcp::resolver resolver_;
};

} // namespace am
