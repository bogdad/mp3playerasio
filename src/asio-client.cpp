#include <absl/functional/any_invocable.h>
#include <absl/log/log.h>
#include <absl/strings/escaping.h>

#include <asio.hpp>
#include <asio/connect.hpp>
#include <asio/detail/socket_ops.hpp>
#include <asio/error.hpp>
#include <asio/error_code.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/registered_buffer.hpp>
#include <asio/strand.hpp>
#include <memory>
#include <string_view>
#include <utility>

#include "asio-client.hpp"
#include "audio-player.hpp"
#include "client-protocol.hpp"
#include "protocol.hpp"

using asio::ip::tcp;

namespace am {

TcpClientConnection::Pointer
TcpClientConnection::create(asio::io_context &io_context,
                            asio::io_context::strand &strand,
                            Mp3Stream &mp3_stream) {
  auto res = std::shared_ptr<TcpClientConnection>(
      new TcpClientConnection(io_context, strand, mp3_stream));
  return res;
}

void TcpClientConnection::on_connect(asio::ip::tcp::endpoint endpoint) {
  auto ptr = shared_from_this();
  receive([ptr = std::move(ptr), this](auto ec) {
    if (ec == asio::error::eof) {
      LOG(INFO) << "client: server closed socket";
    }
  });
}

tcp::socket &TcpClientConnection::socket() { return _socket; }

TcpClientConnection::TcpClientConnection(asio::io_context &io_context,
                                         asio::io_context::strand &strand,
                                         Mp3Stream &mp3_stream)
    : 
    strand_(strand)
    ,_socket(io_context)
    , mp3_stream_(mp3_stream)
    , _client_decoder(
          [this](buffers_2<std::string_view> ts) {
            for (auto sv : ts) {
              LOG(INFO) << "time " << sv;
            }
          },
          [this](RingBuffer &buff) mutable { mp3_stream_.decode_next(); }) {}

void TcpClientConnection::receive(
    std::function<void(const asio::error_code &)> &&on_error) {
  auto ptr = shared_from_this();
  if (mp3_stream_.buffer().buffer().ready_write_size() == 0) {
    mp3_stream_.buffer().add_callback_on_buffer_not_full(
      OnBufferNotFullSz {
      strand_.wrap([ptr, on_error=std::move(on_error)]() mutable {
        ptr->receive(std::move(on_error));
      }),
      1 // as soon as 1 byte is available
    });
  } else {
  _socket.async_read_some(
      mp3_stream_.buffer().buffer().prepared(),
      [this, ptr = std::move(ptr), on_error = std::move(on_error)](
          const asio::error_code &ec, const size_t bytes_transferred) mutable {
        if (ec) {
          LOG(INFO) << "client: received " << mp3_stream_.buffer().buffer() << " error "
                    << ec << " bytes available " << _socket.available();
          on_error(ec);
          // we did not parse the whole read_buffer yet, we schedule a callback
          // to be called once the consumer commits more of read_buffer. make
          // sure connection is alive at this point

          // currently connection is alive because
          // res->_mp3_stream.set_on_low_watermark([res](){
          //   res->handle();
          // });
          // set on low watermark retains tcp connection strongly forever.
        } else {
          ptr->mp3_stream_.buffer().buffer().consume(bytes_transferred);
          LOG(INFO) << "client: received " << bytes_transferred
                    << " from network " << mp3_stream_.buffer().buffer();
          handle();
          receive(std::move(on_error));
        }
      });
  }
}

void TcpClientConnection::handle() {
  _client_decoder.try_read_client(mp3_stream_.buffer().buffer());
  // LOG(INFO) << "client: handled " << mp3_stream_.buffer().buffer();
}

void AsioClient::connect(std::string_view host) {
  resolver_.async_resolve(
      host, "8060", [this](const asio::error_code &, auto results) mutable {
        auto connection =
            TcpClientConnection::create(io_context_, strand_, mp3_stream_);
        auto &socket = connection->socket();
        asio::async_connect(
            socket, results,
            [connection = std::move(connection)](auto ec, auto endpoint) {
              connection->on_connect(endpoint);
            });
      });
}

AsioClient::AsioClient(asio::io_context &io_context,
                       asio::io_context::strand &strand, Mp3Stream &mp3_stream)
    : io_context_(io_context)
    , strand_(strand)
    , mp3_stream_(mp3_stream)
    , resolver_(io_context_) {}

} // namespace am
