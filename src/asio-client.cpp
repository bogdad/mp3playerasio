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
#include <memory>

#include "audio-player.hpp"
#include "client-protocol.hpp"
#include "protocol.hpp"

using asio::ip::tcp;

namespace am {

struct TcpClientConnection : std::enable_shared_from_this<TcpClientConnection> {

  using Pointer = std::shared_ptr<TcpClientConnection>;

  static TcpClientConnection::Pointer create(asio::io_context &io_context, Mp3Stream& mp3stream) {
    return std::shared_ptr<TcpClientConnection>(
        new TcpClientConnection(io_context, mp3stream));
  }

  void on_connect(asio::ip::tcp::endpoint endpoint) {
    auto ptr = shared_from_this();
    receive([ptr = std::move(ptr), this](auto ec) {
      if (ec == asio::error::eof) {
        LOG(INFO) << "client: server closed socket";
      }
    });
  }

  tcp::socket &socket() { return _socket; }

private:
  TcpClientConnection(asio::io_context &io_context, Mp3Stream &mp3stream)
      : _socket(io_context), _read_buffer(8388608),
        _client_decoder(
            [this](buffers_2<std::string_view> ts) {
              for (auto sv : ts) {
                LOG(INFO) << "time " << sv;
              }
            },
            [this, &io_context](RingBuffer &buff) {
              LOG(INFO) << "client: received ring buff";
              _mp3_stream.decode_next(buff, io_context);
            }) {}

  void
  receive(absl::AnyInvocable<void(const asio::error_code &) const> &&on_error) {
    auto ptr = shared_from_this();
    _socket.async_read_some(
        _read_buffer.prepared(),
        [this, ptr = std::move(ptr), on_error = std::move(on_error)](
            const asio::error_code &ec,
            const size_t bytes_transferred) mutable {
          if (ec) {
            LOG(INFO) << "client: received " << _read_buffer << " error " << ec 
              << " bytes available " << _socket.available();
            on_error(ec);
            // we did not parse the whole read_buffer, we schedule a callback to be called once
            // the consumer commits more of read_buffer.
            enqueue_on_commit_func(std::move(ptr));
          } else {
            ptr->_read_buffer.consume(bytes_transferred);
            LOG(INFO) << "client: received from network " << _read_buffer;
            handle();
            receive(std::move(on_error));
          }
        });
  }

  void enqueue_on_commit_func(std::shared_ptr<TcpClientConnection> &&ptr) {
    if (_read_buffer.ready_size() > 0) {
      _read_buffer.enqueue_on_commit_func([this, ptr = std::move(ptr)]() mutable {
        handle();
        enqueue_on_commit_func(std::move(ptr));
      });
    }
  }

  void handle() {
    _client_decoder.try_read_client(_read_buffer);
    LOG(INFO) << "client: handled " << _read_buffer;
  }

  tcp::socket _socket;
  RingBuffer _read_buffer;
  ClientEncoder _client_encoder{};
  ClientDecoder _client_decoder;
  Mp3Stream _mp3_stream;
  DestructionSignaller _destruction_signaller{"TcpClientConnection"};
};

} // namespace am

int main(int argc, char *argv[]) {

  using namespace am;

  std::srand(std::time(nullptr));

  if (argc != 2) {
    LOG(INFO) << "Usage: client <host>" << std::endl;
    return 1;
  }

  std::atomic_int should_stop = 0;

  asio::io_context io_context;
  asio::signal_set signals(io_context, SIGINT);
  signals.async_wait( [&should_stop](const asio::error_code ec, int signal){
    should_stop = 1;
  });
  
  Mp3Stream mp3stream;
  tcp::resolver resolver(io_context);

  resolver.async_resolve(
    argv[1], "8060", [&io_context, &mp3stream](const asio::error_code &, auto results) {
      auto connection = TcpClientConnection::create(io_context, mp3stream);
      asio::async_connect(
          connection->socket(), results,
          [connection = std::move(connection)](auto ec, auto endpoint) {
            connection->on_connect(endpoint);
          });
    });
  const infinite_timer timer(io_context);
  while(should_stop==0) {
    LOG(INFO) << "tick " << io_context.stopped();
    io_context.run_one();
  }
  LOG(INFO) << "shutting down";
  fflush(stdout); fflush(stderr);

  // here we need to unschedule all on commit callbacks, they are preventing ring buffer to die.
  return 0;
}
