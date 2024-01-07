#include "util.hpp"

#include <absl/log/log.h>

namespace am {


DestructionSignaller::DestructionSignaller(std::string &&name)
    : name_(std::move(name)) {
  LOG(INFO) << "constructing " << name_;
}

DestructionSignaller::~DestructionSignaller() {
  LOG(INFO) << "destroying " << name_;
}

DestructionSignaller::DestructionSignaller(DestructionSignaller &&other) noexcept: name_(other.name_) {

}

}
