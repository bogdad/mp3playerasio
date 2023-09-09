#include "mp3.hpp"
#include <absl/log/log.h>
#include <algorithm>
#include <asio.hpp>
#include <asio/ip/tcp.hpp>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <fstream>
#include <optional>
#include <sys/socket.h>

namespace am {

mp3 mp3::create(fs::path filepath) {
  printf("filepath %s\n", filepath.c_str());
  if (!std::filesystem::exists(filepath)) {
    LOG(ERROR) << "file " << filepath << " does not exist";
    std::terminate();
  }
  auto sz = std::filesystem::file_size(filepath);
  FILE *fd = fopen(filepath.c_str(), "rb");
  fhandle f{fd};

  return mp3(std::move(f), sz);
}

int mp3::send_chunk(const asio::ip::tcp::socket &socket) {
  off_t len;
  return call_sendfile(socket, len);
}

int mp3::call_sendfile(const asio::ip::tcp::socket &socket, off_t &len) {
  len = _current.len();
  auto &non_const_socket = const_cast<asio::ip::tcp::socket &>(socket);
  int res = sendfile(fileno(_fd.get()),
                     non_const_socket.lowest_layer().native_handle(),
                     _current._current, &len, nullptr, 0);
  LOG(INFO) << "sent " << len;
  if (res == 0) {
    _current.consume(len);
  } else {
    int err = errno;
    if (err == EAGAIN) {
      _current.consume(len);
      return 0;
    } else {
      LOG(ERROR) << "sendfile failed " << res << " errno " << err;
      std::terminate();
    }
  }
  return res;
}

bool mp3::is_all_sent() const { return _current._current == _current._size; }

size_t mp3::size() const { return _size; }

off_t file_view::len() const { return _size - _current; }

void file_view::consume(size_t l) {
  if (l > len())
    std::terminate();
  _current += l;
}

}
