#pragma once

#include <absl/functional/any_invocable.h>
#include <array>
#include <asio.hpp>
#include <asio/buffer.hpp>
#include <exception>
#include <span>
#include <string>
#include <string_view>
#include <vector>

/*

Protocol:

client connects
server sends time
clients asks mp3
server sends mp3

 */

using mutable_buffers = std::vector<asio::mutable_buffer>;
using const_buffers = std::vector<asio::const_buffer>;

using bytes_view = std::span<const char>;

struct ReadBuffer {

  // parsing part
  int nw() const;
  int peek_int() const;
  std::string_view peek_string_view(int len) const;
  bytes_view peek_span(int len) const;

  void skip_len(int len);
  void check(int len, std::string_view method) const;

  // socket reading
  asio::mutable_buffer next_buffer();
  void advance_buffer(int len);

  std::array<char, 2048> _buff0;
  std::array<char, 2048> _buff1;

  std::span<char> buff();
  std::span<const char> cbuff() const;

  int _curr;
  int _last_written;
  int _cur_buffer = 0;
};

struct Envelope {
  int message_type;
  int message_size;
};

enum class DecoderState { before_envelope = 0, have_envelope };

struct Decoder {
  DecoderState _state{};
  Envelope _envelope{};

  bool try_read(ReadBuffer &state);
  void reset();
};

struct WriteBuffer {

  void memcpy_in(const void *data, size_t sz);
  asio::const_buffer as_buffer() const;

  std::array<char, 2048> _arr{};
  int _last_written{};
};

struct Encoder {
  void fill_envelope(Envelope envelope, WriteBuffer &buff);
};
