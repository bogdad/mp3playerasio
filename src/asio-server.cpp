#include <asio.hpp>
#include <iostream>
#include <ostream>

using asio::ip::tcp;

std::string make_daytime_string() {
  using namespace std; // For time_t, time and ctime;
  time_t now = time(0);
  return ctime(&now);
}

int main(int argc, char **argv) {
  try {
    asio::io_context io_context;

    tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), 8060));
    for (;;) {
      tcp::socket socket(io_context);
      acceptor.accept(socket);
      std::string message = make_daytime_string();
      asio::error_code ignored_error;
      asio::write(socket, asio::buffer(message), ignored_error);
    }
  } catch (std::exception &ex) {
    std::cout << ex.what() << std::endl;
  }
  return 0;
}
