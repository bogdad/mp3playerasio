#include "protocol-system.hpp"

#include <cstddef>
#include <exception>
#include <sstream>
#include <string>
#include <type_traits>

#if defined(__APPLE__) || defined(__linux__)
#  include <fcntl.h>
#  include <sys/mman.h>
#  include <unistd.h>
#elif defined(_WIN32) || defined(_WIN64)
#  include <windows.h>
#  include <conio.h>
#  include <processthreadsapi.h>
#  include <sysinfoapi.h>
#  include <tchar.h>
#endif

namespace am {

void free(LinearMemInfo &info) {
#if defined(__APPLE__) || defined(__linux__)
  if (info._p1)
    munmap(info._p1, info._len);
  if (info._p2)
    munmap(info._p2, info._len);
  if (!info._shname.empty())
    shm_unlink(info._shname.c_str());
#endif
}

LinearMemInfo::LinearMemInfo(std::size_t minsize) {
  int res = init(minsize);
  if (res != 0) {
    std::terminate();
  };
}

LinearMemInfo::~LinearMemInfo() {
  if (_res != 0)
    free(*this);
}

int LinearMemInfo::init(std::size_t minsize) {
  _res = -1;
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
  int fd = shm_open(shname.c_str(), O_RDWR | O_CREAT);
  _shname = shname;
  std::size_t len = bytes;
  if (ftruncate(fd, len) == -1) {
    perror("ftruncate");
    return -1;
  }
  void *p = ::mmap(nullptr, 2 * len, PROT_WRITE, MAP_ANON | MAP_SHARED, -1, 0);
  if (p == MAP_FAILED) {
    perror("mmap");
    return -1;
  }
  munmap(p, 2 * len);

  _p1 = (char *)mmap(p, len, PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0);
  if (_p1 == MAP_FAILED) {
    perror("mmap1");
    return -1;
  }

  _p2 = (char *)mmap((char *)p + len, len, PROT_WRITE, MAP_SHARED | MAP_FIXED,
                     fd, 0);
  if (_p2 == MAP_FAILED) {
    perror("mmap2");
    return -1;
  }
  _p1[0] = 'x';
  printf("pointer %s: %p %p %p %ld %c %c\n", _shname.c_str(), p, _p1, _p2,
         (char *)_p2 - (char *)_p1, _p1[0], _p2[0]);
  _len = len;
  _p1[0] = 0;
  _res = 0;
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
  // alloc 2x size then free, hoping that next map would be exactly at this addr
  void *ptr = VirtualAlloc(0, alloc_size, MEM_RESERVE, PAGE_NOACCESS);
  if (!ptr) {
    return -1;
  }
  VirtualFree(ptr, 0, MEM_RELEASE);

  if (!(_file_handle =
            CreateFileMappingA(INVALID_HANDLE_VALUE, 0, PAGE_READWRITE,
                               (unsigned long long)alloc_size >> 32,
                               alloc_size & 0xffffffffu, 0))) {
    return -1;
  }
  if (!(_p1 = (char *)MapViewOfFileEx(_file_handle, FILE_MAP_ALL_ACCESS, 0, 0,
                                      len, ptr))) {
    return -1;
  }

  if (!(_p2 = (char *)MapViewOfFileEx(_file_handle, FILE_MAP_ALL_ACCESS, 0, 0,
                                      len, (char *)ptr + len))) {
    // something went wrong - clean up
    // TODO: cleanup. its works as is because we are calling terminate on -1
    return -1;
  }
  _len = len;
  _res = 0;
#endif
  return 0;
}

std::size_t system_page_size() {
#if defined(_WIN32) || defined(_WIN64)
  SYSTEM_INFO sysInfo;
  GetSystemInfo(&sysInfo);
  return sysInfo.dwAllocationGranularity;
#else
  std::terminate();
#endif
}

} // namespace am
