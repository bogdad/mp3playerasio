#pragma once

#include <asio.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/windows/object_handle.hpp>
#include <cstddef>
#include <cstdio>
#include <memory>

#if defined (__LINUX__) || defined(__APPLE__)
#include <sys/types.h>
#elif defined (_WIN32) || defined (_WIN64)
#include <minwinbase.h>
#endif


namespace am {

struct file_view {
  size_t _current;
  size_t _size;
  off_t len() const;
  void consume(size_t len);
};

struct SendFile {
  SendFile(asio::io_context &io_context, asio::ip::tcp::socket &socket, std::FILE *file, std::size_t size);
  SendFile(const SendFile &) = delete;
  SendFile(SendFile &&) = default;
  SendFile& operator=(const SendFile &) = delete;
  SendFile& operator=(SendFile &&) = delete;
  ~SendFile();
private:
  void call();

  asio::io_context &io_context_;
  asio::ip::tcp::socket &socket_;
  std::FILE *file_;
  std::size_t cur_;
  std::size_t size_;
#if defined (__LINUX__) || defined(__APPLE__)
  off_t len_;
#elif defined (_WIN32) || defined (_WIN64)
  OVERLAPPED overlapped_{};
  std::unique_ptr<asio::windows::object_handle> event_{};
#endif
};

} // namespace am
