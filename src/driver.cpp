#include "driver.hpp"
#include "audio-player.hpp"
#include "client-protocol.hpp"
#include "protocol.hpp"

#include <absl/log/log.h>
#include <asio/io_context.hpp>

using asio::ip::tcp;

namespace am {

Driver::Driver(asio::io_context &io_context, asio::io_context::strand &strand,
               std::string &&host):
    context_(io_context)
    , strand_(strand)
    , work_guard_(io_context.get_executor())
    , host_(std::move(host))
    , mp3_stream_(buffer_, io_context, strand) {}

void Driver::play(Song &&song) {
  asio_client_.emplace(context_, strand_, mp3_stream_);
  asio_client_->connect(host_);
}

} // namespace am

int main(int argc, char *argv[]) {
  using namespace am;

  std::srand(std::time(nullptr));

  if (argc != 2) {
    LOG(INFO) << "Usage: driver <host>" << std::endl;
    return 1;
  }

  std::atomic_int should_stop = 0;

  asio::io_context io_context;
  asio::io_context::strand strand{io_context};
  asio::signal_set signals(io_context, SIGINT);
  signals.async_wait([&should_stop](const asio::error_code ec, int signal) {
    should_stop = 1;
  });

  auto driver = am::Driver(io_context, strand, argv[1]);
  driver.play({});

  while (!should_stop) {
    io_context.run_one();
  }
  LOG(INFO) << "shutting down";
  fflush(stdout);
  fflush(stderr);

  // here we need to unschedule all on commit callbacks, they are preventing
  // ring buffer to die.
  return 0;
}
