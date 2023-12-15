#include "driver.hpp"
#include "audio-player.hpp"
#include "client-protocol.hpp"
#include "protocol.hpp"

#include <absl/log/log.h>
#include <asio/io_context.hpp>

using asio::ip::tcp;

namespace am {

Driver::Driver(asio::io_context &io_context, asio::io_context::strand &strand): buffer_(16000000, 40000, 80000), context_(io_context),
work_guard_(io_context.get_executor()), mp3_stream_(buffer_, io_context, strand) {

}

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
  asio::io_context::strand strand{io_context};
  asio::executor_work_guard<asio::io_context::executor_type> wg = asio::make_work_guard(io_context);
  asio::signal_set signals(io_context, SIGINT);
  signals.async_wait( [&should_stop](const asio::error_code ec, int signal){
    should_stop = 1;
  });
  
  auto driver = am::Driver(io_context);
  driver.play({});

  while (!should_stop) {
    io_context.run_one(); 
  }
  LOG(INFO) << "shutting down";
  fflush(stdout); fflush(stderr);

  // here we need to unschedule all on commit callbacks, they are preventing ring buffer to die.
  return 0;
}
