#include "protocol-system.hpp"

#include <cstddef>
#include <exception>
#include <sstream>
#include <string>

#if defined(_WIN32) || defined(_WIN64)
#  include <windows.h>
#endif

#if defined(__APPLE__) || defined(__linux__)
#  include <fcntl.h>
#  include <sys/mman.h>
#  include <unistd.h>
#elif defined(_WIN32) || defined(_WIN64)
#  include <conio.h>
#  include <processthreadsapi.h>
#  include <sysinfoapi.h>
#  include <tchar.h>
#endif

namespace am {

  void free(LinearMemInfo & info) {
#if defined(__APPLE__) || defined(__linux__)
    if (info.p1_)
      munmap(info.p1_, info.len_);
    if (info.p2_)
      munmap(info.p2_, info.len_);
    if (!info.shname_.empty())
      shm_unlink(info.shname_.c_str());
#endif
  }

  LinearMemInfo::LinearMemInfo(std::size_t minsize) {
    int res = init(minsize);
    if (res != 0) {
      std::terminate();
    };
  }

  LinearMemInfo::~LinearMemInfo() {
    if (res_ != 0)
      free(*this);
  }

  int LinearMemInfo::init(std::size_t minsize) {
    res_ = -1;
// TODO: this leaks resources in error
#if defined(__APPLE__) || defined(__linux__)
    // source https: // github.com/lava/linear_ringbuffer
    pid_t pid = getpid();
    static int counter = 0;
    std::size_t pagesize = ::sysconf(_SC_PAGESIZE);
    std::size_t bytes = minsize & ~(pagesize - 1);
    if (minsize % pagesize) {
      bytes += pagesize;
    }
    if (bytes * 2u < bytes) {
      errno = EINVAL;
      perror("overflow");
      return -1;
    }
    int r = counter++;
    std::stringstream s;
    s << "pid_" << pid << "_buffer_" << r;
    const auto shname = s.str();
    shm_unlink(shname.c_str());
    int fd = shm_open(shname.c_str(), O_RDWR | O_CREAT, 0);
    shname_ = shname;
    std::size_t len = bytes;
    if (ftruncate(fd, len) == -1) {
      perror("ftruncate");
      return -1;
    }
    void *p =
        ::mmap(nullptr, 2 * len, PROT_WRITE, MAP_ANON | MAP_SHARED, -1, 0);
    if (p == MAP_FAILED) {
      perror("mmap");
      return -1;
    }
    munmap(p, 2 * len);

    p1_ = (char *)mmap(p, len, PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0);
    if (p1_ == MAP_FAILED) {
      perror("mmap1");
      return -1;
    }

    p2_ = (char *)mmap((char *)p + len, len, PROT_WRITE, MAP_SHARED | MAP_FIXED,
                       fd, 0);
    if (p2_ == MAP_FAILED) {
      perror("mmap2");
      return -1;
    }
    p1_[0] = 'x';
    printf("pointer %s: %p %p %p %ld %c %c\n", shname_.c_str(), p, p1_, p2_,
           (char *)p2_ - (char *)p1_, p1_[0], p2_[0]);
    if (p1_[0] != p2_[0]) {
      perror("not the same memory");
      return -1;
    }
    len_ = len;
    res_ = 0;
#else
  // source https://gist.github.com/rygorous/3158316
  DWORD pid = GetCurrentProcessId();
  static int counter = 0;
  std::size_t pagesize = system_page_size();
  std::size_t bytes = minsize & ~(pagesize - 1);
  if (minsize % pagesize) {
    bytes += pagesize;
  }
  if (bytes * 2u < bytes) {
    errno = EINVAL;
    perror("overflow");
    return -1;
  }
  int r = counter++;
  std::stringstream s;
  s << "pid_" << pid << "_buffer_" << r;
  const auto shname = s.str();
  std::size_t len = bytes;
  std::size_t alloc_size = 2 * len;
  // alloc 2x size then free, hoping that next map would be exactly at this
  // addr
  void *ptr = VirtualAlloc(0, alloc_size, MEM_RESERVE, PAGE_NOACCESS);
  if (!ptr) {
    return -1;
  }
  VirtualFree(ptr, 0, MEM_RELEASE);

  if (!(file_handle_ =
            CreateFileMappingA(INVALID_HANDLE_VALUE, 0, PAGE_READWRITE,
                               (unsigned long long)alloc_size >> 32,
                               alloc_size & 0xffffffffu, 0))) {
    return -1;
  }
  if (!(p1_ = (char *)MapViewOfFileEx(file_handle_, FILE_MAP_ALL_ACCESS, 0, 0,
                                      len, ptr))) {
    return -1;
  }

  if (!(p2_ = (char *)MapViewOfFileEx(file_handle_, FILE_MAP_ALL_ACCESS, 0, 0,
                                      len, (char *)ptr + len))) {
    // something went wrong - clean up
    // TODO: cleanup. its works as is because we are calling terminate on -1
    return -1;
  }
  p1_[0] = 'x';
  if (p1_[0] != p2_[0]) {
      return -1;
  }
  len_ = len;
  res_ = 0;
#endif
    return 0;
  }

  std::size_t system_page_size() {
#if defined(_WIN32) || defined(_WIN64)
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    return sysInfo.dwAllocationGranularity;
#else
    return ::sysconf(_SC_PAGESIZE);
#endif
  }

} // namespace am
