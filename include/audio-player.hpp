#pragma once

#include "protocol.hpp"
#include <memory>

namespace asio {
  struct io_context;
}

namespace am {

struct Mp3Stream {
public:
  Mp3Stream();
  void decode_next(RingBuffer &input, asio::io_context &io_context);
private:
  struct Pimpl;
  struct PimplDeleter {
    void operator()(Pimpl *);
  };
  std::unique_ptr<Pimpl, PimplDeleter> _pimpl;
};

} // namespace am
