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
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <ostream>
#include <sstream>
#include <string_view>
#include <sys/socket.h>
#include <thread>
#include <utility>

#include "client-protocol.hpp"
#include "protocol.hpp"

using asio::ip::tcp;

namespace am {

struct TcpClientConnection: std::enable_shared_from_this<TcpClientConnection> {
  
  using Pointer = std::shared_ptr<TcpClientConnection>;

  static TcpClientConnection::Pointer create(asio::io_context &io_context) {
    return std::shared_ptr<TcpClientConnection>(new TcpClientConnection(io_context));
  }

  void on_connect(asio::ip::tcp::endpoint endpoint) {
    auto ptr = shared_from_this();
    receive([ptr = std::move(ptr), this](auto ec){
      if (ec == asio::error::eof) {
        LOG(INFO) << "client: server closed socket";
        _socket.close();
      }
    });
  }

  tcp::socket& socket() {
    return _socket;
  }

private:
  TcpClientConnection(asio::io_context &io_context): _socket(io_context), _read_buffer(8388608),
  _client_decoder([this](buffers_2<std::string_view> ts) {
      for (auto sv : ts) {
        LOG(INFO) << "time " << sv;
      }
      int rand_delay = arc4random() % 15;
      std::cout << "sleeping for " << rand_delay << std::endl;

      std::this_thread::sleep_for(std::chrono::seconds(rand_delay));
    }, [](buffers_2<bytes_view> ts) {
      for (auto spn : ts) {
        std::string encoded;
        absl::Base64Escape(absl::string_view(spn.data(), spn.size()), &encoded);
        std::cout << "finished reading " << encoded << std::endl;
      }
    }) {
  }
  
  void
  receive(absl::AnyInvocable<void(const asio::error_code &) const> &&on_error) {
    auto ptr = shared_from_this();
    _socket.async_read_some(
        _read_buffer.prepared(), [this, ptr=std::move(ptr), on_error = std::move(on_error)](
                                  const asio::error_code &ec,
                                  const size_t bytes_transferred) mutable {
          LOG(INFO) << "client: received " << _read_buffer;
          if (ec) {
            on_error(ec);
          } else {
            ptr->_read_buffer.consume(bytes_transferred);
            ptr->_client_decoder.try_read_client(ptr->_read_buffer);
            receive(std::move(on_error));
          }
        });
  }

  tcp::socket _socket;
  RingBuffer _read_buffer;
  ClientEncoder _client_encoder{};
  ClientDecoder _client_decoder;
};

}// namespace am


int main(int argc, char *argv[]) {

  using namespace am;

  std::srand(std::time(nullptr));
  
  if (argc != 2) {
    std::cerr << "Usage: client <host>" << std::endl;
    return 1;
  }
  asio::io_context io_context;
  tcp::resolver resolver(io_context);
  //tcp::resolver::results_type endpoints = resolver.resolve(argv[1], "8060");
  resolver.async_resolve(argv[1], "8060", [&io_context](const asio::error_code &, auto results){
    auto connection = TcpClientConnection::create(io_context);
    asio::async_connect(connection->socket(), results, [connection=std::move(connection)](auto ec, auto endpoint){
      connection->on_connect(endpoint);
    });
  });
  io_context.run();
}
