#include "mp3.hpp"
#include "protocol.hpp"
#include <absl/base/macros.h>
#include <absl/log/log.h>
#include <algorithm>
#include <asio.hpp>
#include <asio/ip/tcp.hpp>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <fstream>
#include <optional>
#include <span>
#include <sys/socket.h>

namespace am {

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

/*
inspiration: https://hackage.haskell.org/package/mp3decoder-0.0.1/src/Codec/Audio/MP3/Unpack.hs
*/

struct Mp3Frame {
  std::byte audio_version_id : 3;
  std::byte layer_description : 2;
  std::byte protection_bit : 1;
  std::byte bitrate_index: 4;
  std::byte sampling_rate_frequency_index: 2;
  std::byte padding_bit: 1;
  std::byte channel_mode: 2;
  std::byte mode_extension: 2;
  std::byte copyright: 1;
  std::byte original: 1;
  std::byte emphasis: 2;
};

struct Mp3FrameParser {

  std::optional<Mp3Frame> try_read(buffers_2<std::span<std::byte>> data) {
    if (data.size() < 4) return {};
    Mp3Frame res{};
    ABSL_ASSERT(data[0] == std::byte{255});
    ABSL_ASSERT((data[1] >> (8 - 3)) & std::byte{0b11});
    res.audio_version_id = ((data[1] & std::byte{0b00011111}) >> 3);
    res.layer_description = ((data[1] & std::byte{0b00000111}) >> 1);
    res.protection_bit = ((data[1] & std::byte{0b00000001}));
    res.bitrate_index = ((data[2] & std::byte{0b11110000}) >> 4);
    res.sampling_rate_frequency_index = ((data[2] & std::byte{0b00001100}) >> 2);
    res.padding_bit = ((data[2] & std::byte{0b00000010}) >> 1);
    res.channel_mode = ((data[3] & std::byte{0b11000000}) >> 6);
    res.mode_extension = ((data[3] & std::byte{0b00110000}) >> 4);
    res.copyright = ((data[3] & std::byte{0b00001000}) >> 3);
    res.original = ((data[3] & std::byte{0b00000100}) >> 2);
    res.emphasis = ((data[3] & std::byte{0b00000011}));
    return res;
  }
};

} // namespace am
