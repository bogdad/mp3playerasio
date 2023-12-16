#pragma once

#include <absl/functional/any_invocable.h>
#include <absl/log/log.h>
#include <asio.hpp>
#include <asio/error_code.hpp>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <memory>

namespace am {

namespace fs = std::filesystem;

struct file_deleter {
  void operator()(std::FILE *fp) { std::fclose(fp); }
};

using fhandle = std::unique_ptr<std::FILE, file_deleter>;

struct file_view {
  size_t _current;
  size_t _size;
  off_t len() const;
  void consume(size_t len);
};

struct Mp3 {

  static Mp3 create(fs::path filepath);
  std::size_t size() const;
  int send_chunk(const asio::ip::tcp::socket &socket);
  bool is_all_sent() const;

private:
  Mp3(fhandle fd, size_t size)
      : _fd(std::move(fd))
      , _size(size)
      , _current({0, size}){};
  int call_sendfile(const asio::ip::tcp::socket &socket, off_t &len);

  fhandle _fd;
  std::size_t _size;
  bool _started{false};
  file_view _current;
};

} // namespace am
