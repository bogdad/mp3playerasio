#include "mp3.hpp"
#include "protocol.hpp"
#include <absl/base/macros.h>
#include <absl/log/log.h>
#include <algorithm>
#include <asio.hpp>
#include <asio/ip/tcp.hpp>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <fstream>
#include <optional>
#include <span>
#include <sys/socket.h>

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#include <minimp3.h>

namespace am {

struct Mp3Stream::Pimpl {
  mp3dec_t mp3d{};

  Pimpl() {
    mp3dec_init(&mp3d);
  }

  void decode_next() {
    mp3dec_frame_info_t info;
    short pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    /*unsigned char *input_buf; - input byte stream*/
    //int samples = mp3dec_decode_frame(&mp3d, input_buf, buf_size, pcm, &info);
  }
};



Mp3 Mp3::create(fs::path filepath) {
  LOG(INFO) << "filepath " << filepath;
  if (!std::filesystem::exists(filepath)) {
    LOG(ERROR) << "file " << filepath << " does not exist";
    std::terminate();
  }
  auto sz = std::filesystem::file_size(filepath);
  FILE *fd = fopen(filepath.c_str(), "rb");
  fhandle f{fd};

  return {std::move(f), sz};
}

int Mp3::send_chunk(const asio::ip::tcp::socket &socket) {
  off_t len = 0;
  return call_sendfile(socket, len);
}

int Mp3::call_sendfile(const asio::ip::tcp::socket &socket, off_t &len) {
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

bool Mp3::is_all_sent() const { return _current._current == _current._size; }

size_t Mp3::size() const { return _size; }

off_t file_view::len() const { return _size - _current; }

void file_view::consume(size_t l) {
  if (l > len())
    std::terminate();
  _current += l;
}



} // namespace am
