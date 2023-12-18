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

  int init(std::size_t);

  int res_{};
  std::string shname_{};
  void *file_handle_{};
  char *p1_{};
  char *p2_{};

  std::size_t len_{};
};

std::size_t system_page_size();

} // namespace am
