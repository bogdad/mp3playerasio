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
    State state {};

    auto on_time = [&socket](std::string_view ts){
      LOG(INFO) << "time " << ts;
      int rand_delay = std::rand() % 15;
      std::cout << "sleeping for " << rand_delay << std::endl;

      std::this_thread::sleep_for(std::chrono::seconds(rand_delay));

      std::stringstream str;
      str << "we reply this to it " << rand_delay;
      std::string strstr = str.str();

      asio::error_code error;
      auto nwrote = socket.write_some(
          asio::buffer(strstr.c_str(), strstr.length() + 1), error);
      std::cout << "write error: " << error << std::endl;
      if (error == asio::error::eof) {
        std::cout << "eof!" << std::endl;
      }
      std::cout << "wrote " << nwrote << " of " << strstr << std::endl;
    };
    auto on_mp3_bytes = [](bytes_view ts) { 
      std::string encoded;
      absl::Base64Escape(absl::string_view(ts.data(), ts.size()), &encoded);
      std::cout << "finished reading " << encoded << std::endl;
    };

    ClientHandler handler(on_time, on_mp3_bytes);
    for (;;) {
      asio::error_code error;

      size_t len = socket.read_some(state.next_buffer(), error);
      state.advance_buffer(len);

      handler.try_read_client(state);
      if (error == asio::error::eof) {
        std::cout << "eof!" << std::endl;
        break;
      }
      if (error)
        throw asio::system_error(error); // Some other error.
    }
  } catch (std::exception &e) {
    std::cout << "ex: " << e.what() << std::endl;
  }
}
