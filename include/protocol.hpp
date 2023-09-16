#pragma once

#include <absl/base/macros.h>
#include <absl/functional/any_invocable.h>
#include <absl/strings/str_format.h>
#include <array>
#include <asio.hpp>
#include <asio/buffer.hpp>
#include <asio/error_code.hpp>
#include <cstddef>
#include <exception>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

/*

Protocol:

client connects
server sends time
sends some message
server sends mp3

 */

namespace am {

using mutable_buffers = std::vector<asio::mutable_buffer>;
using const_buffers = std::vector<asio::const_buffer>;

using bytes_view = std::span<const char>;

template <typename Buffer> struct buffers_2 {
  using value_type = Buffer;
  using const_iterator = const value_type *;
  buffers_2() : _buffer_count(0){};
  explicit buffers_2(const value_type &buffer) : _buffer_count(1) {
    _buffers[0] = buffer;
  }
  buffers_2(const value_type &buffer1, const value_type &buffer2)
      : _buffer_count(2) {
    _buffers[0] = buffer1;
    _buffers[1] = buffer2;
  }

  const_iterator begin() const { return std::addressof(_buffers[0]); };
  const_iterator end() const {
    return std::addressof(_buffers[0]) + _buffer_count;
  }
  bool empty() const { return !_buffer_count; }

  std::size_t size() const {
    if (_buffer_count == 0) return 0;
    auto res = size_t(_buffers[0].size());
    if (_buffer_count == 2)
      res += _buffers[1].size();
    return res;
  }

  inline std::byte& operator[](size_t pos) const {
    ABSL_ASSERT(_buffer_count > 0);
    auto b0size = _buffers[0].size();
    if (pos < b0size) {
      return _buffers[0][pos];
    }
    return _buffers[1][pos - b0size];
  }

private:
  std::array<Buffer, 2> _buffers;
  std::size_t _buffer_count;
};

struct RingBuffer {
  RingBuffer(std::size_t size);

  using const_buffers_type = buffers_2<asio::const_buffer>;
  using mutable_buffers_type = buffers_2<asio::mutable_buffer>;

  void reset();

  /// Reduce filled sequence by marking first size bytes of filled sequence as
  /// nonfilled sequence.
  /**
   * Doesn't move or copy anything. Size of nonfilled sequence grows up by size
   * bytes. Start of nonfilled sequence doesn't change. Size of filled sequence
   * reduces by size bytes. Start of filled sequence moves up (circular) by
   * size bytes.
   */
  void commit(std::size_t len);

  /// Reduce nonfilled sequence by marking first size bytes of
  /// nonfilled sequence as filled sequence.
  /**
   * Doesn't move or copy anything. Size of filled sequence grows up by size
   * bytes. Start of filled sequence doesn't change. Size of nonfilled sequence
   * reduces by size bytes. Start of nonfilled sequence moves up (circular) by
   * size bytes.
   */
  void consume(std::size_t size);

  void memcpy_in(const void *data, size_t sz);

  // non filled sequence for writes
  mutable_buffers_type prepared();
  mutable_buffers_type prepared(std::size_t max_size);

  const_buffers_type data() const;
  const_buffers_type data(std::size_t max_size) const;

  bool empty() const;
  size_t ready_size() const;

  template <typename Sink>
  friend void AbslStringify(Sink &sink, const RingBuffer &buffer) {
    absl::Format(&sink, "[(%zu, %zu)],[(%zu, %zu)]", buffer._filled_start,
                 buffer._filled_size, buffer._non_filled_start,
                 buffer._non_filled_size);
  }

  void check(int len, std::string_view method) const;
  inline char char_at(std::size_t pos) const;
  int peek_int() const;
  buffers_2<std::string_view> peek_string_view(int len) const;
  buffers_2<bytes_view> peek_span(int len) const;

private:
  std::size_t _size;
  std::vector<char> _data;
  std::size_t _filled_start;
  std::size_t _filled_size;
  std::size_t _non_filled_start;
  std::size_t _non_filled_size;
};

struct Envelope {
  void log();
  int message_type;
  int message_size;
};

enum class DecoderState { before_envelope = 0, have_envelope };

struct Decoder {
  DecoderState _state{};
  Envelope _envelope{};

  bool try_read(RingBuffer &state);
  void reset();
};

struct Encoder {
  void fill_envelope(Envelope envelope, RingBuffer &buff);
};
} // namespace am
