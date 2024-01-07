#pragma once

#include <string>

namespace am {

struct DestructionSignaller {
  std::string name_;
  DestructionSignaller(std::string &&name);
  DestructionSignaller(const DestructionSignaller&) = default;
  DestructionSignaller(DestructionSignaller&&) noexcept;
  ~DestructionSignaller();
};


}
