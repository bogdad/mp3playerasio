#pragma once

#include "protocol.hpp"
#include <memory>

namespace asio {
  struct io_context;
}

namespace am {

struct Mp3Stream {
public:
  Mp3Stream(RingBuffer &input, asio::io_context &io_context, asio::io_context::strand &strand);
  void decode_next();
private:
  struct Pimpl;
  struct PimplDeleter {
    void operator()(Pimpl *);
  };
  std::unique_ptr<Pimpl, PimplDeleter> _pimpl;
};

} // namespace am
