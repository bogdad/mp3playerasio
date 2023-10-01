#pragma once

#include "protocol.hpp"
#include <memory>

namespace am {

struct Mp3Stream {
public:
  Mp3Stream();
  void decode_next(RingBuffer &input);
private:
  struct Pimpl;
  struct PimplDeleter {
    void operator()(Pimpl *);
  };
  std::unique_ptr<Pimpl, PimplDeleter> _pimpl;
};

} // namespace am
