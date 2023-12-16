#pragma once

#include <cstddef>
#include <string>
namespace am {

struct LinearMemInfo {
  LinearMemInfo(std::size_t);
  ~LinearMemInfo();
  LinearMemInfo(const LinearMemInfo &) = delete;
  LinearMemInfo(LinearMemInfo &&) = delete;
  LinearMemInfo &operator=(const LinearMemInfo &) = delete;
  LinearMemInfo &operator=(LinearMemInfo &&) = delete;

  int init_posix(std::size_t);

  int _res{};
  std::string _shname{};
  char *_p1{};
  char *_p2{};
  std::size_t _len{};
};

} // namespace am
