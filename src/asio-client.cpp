#include <absl/log/log.h>
#include <absl/strings/escaping.h>
#include <asio.hpp>
#include <asio/detail/socket_ops.hpp>
#include <asio/error.hpp>
#include <asio/registered_buffer.hpp>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <ostream>
#include <sstream>
#include <string_view>
#include <thread>

#include "client-protocol.hpp"
#include "protocol.hpp"

using asio::ip::tcp;

int main(int argc, char *argv[]) {

  using namespace am;

  std::srand(std::time(nullptr));
  try {
    if (argc != 2) {
      std::cerr << "Usage: client <host>" << std::endl;
      return 1;
    }
    asio::io_context io_context;
    tcp::resolver resolver(io_context);
    tcp::resolver::results_type endpoints = resolver.resolve(argv[1], "8060");
    tcp::socket socket(io_context);
    asio::connect(socket, endpoints);
    RingBuffer read_buffer{8388608};

    ClientEncoder client_encoder {};

    auto on_time = [&socket](buffers_2<std::string_view> ts) {
      for (auto sv: ts) {
        LOG(INFO) << "time " << sv;
      }
      int rand_delay = arc4random() % 15;
      std::cout << "sleeping for " << rand_delay << std::endl;

      std::this_thread::sleep_for(std::chrono::seconds(rand_delay));
    };
    auto on_mp3_bytes = [](buffers_2<bytes_view> ts) {
      for( auto spn: ts) {
        std::string encoded;
        absl::Base64Escape(absl::string_view(spn.data(), spn.size()), &encoded);
        std::cout << "finished reading " << encoded << std::endl;
      }
    };

    ClientDecoder handler(on_time, on_mp3_bytes);
    for (;;) {
      asio::error_code error;
      LOG(INFO) << "read_buffer curr " << read_buffer;
      size_t len = socket.read_some(read_buffer.prepared(), error);
      LOG(INFO) << "read " << len;
      read_buffer.consume(len);

      handler.try_read_client(read_buffer);
      if (error == asio::error::eof) {
        std::cout << "eof!" << std::endl;
        break;
      }
      if (error) {
        throw asio::system_error(error); // Some other error.
      }
    }
  } catch (std::exception &e) {
    std::cout << "ex: " << e.what() << std::endl;
  }
}
