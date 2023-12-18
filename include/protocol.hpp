#pragma once

#include "protocol-system.hpp"
#include <absl/functional/any_invocable.h>
#include <absl/strings/str_format.h>
#include <array>
#include <asio.hpp>
#include <asio/buffer.hpp>
#include <asio/error_code.hpp>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <mutex>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

/*

Protocol:

client connects
server sends time
client asks to send offset
server sends mp3 offset

 */

namespace am {

struct DestructionSignaller {
  std::string name;
  ~DestructionSignaller();
};

using mutable_buffers = std::vector<asio::mutable_buffer>;
using const_buffers = std::vector<asio::const_buffer>;

using bytes_view = std::span<const char>;

template <typename Buffer> struct buffers_2 {
  using value_type = Buffer;
  using const_iterator = const value_type *;
  buffers_2()
      : buffer_count_(0){};
  explicit buffers_2(const value_type &buffer)
      : buffer_count_(1) {
    buffers_[0] = buffer;
  }
  buffers_2(const value_type &buffer1, const value_type &buffer2)
      : buffer_count_(2) {
    buffers_[0] = buffer1;
    buffers_[1] = buffer2;
  }

  const_iterator begin() const { return std::addressof(buffers_[0]); }
  const_iterator end() const {
    return std::addressof(buffers_[0]) + buffer_count_;
  }
  bool empty() const { return !buffer_count_; }

  std::size_t size() const {
    if (buffer_count_ == 0)
      return 0;
    auto res = size_t(buffers_[0].size());
    if (buffer_count_ == 2)
      res += buffers_[1].size();
    return res;
  }

  inline std::byte &operator[](size_t pos) const {
    auto b0size = buffers_[0].size();
    if (pos < b0size) {
      return buffers_[0][pos];
    }
    return buffers_[1][pos - b0size];
  }

  inline std::size_t count() const { return buffer_count_; }

private:
  std::array<Buffer, 2> buffers_;
  std::size_t buffer_count_;
};

struct LinnearArray {
  LinnearArray(std::size_t size);
  std::size_t size() const;
  inline char &at(std::size_t pos) { return *(ptr_ + pos); }
  inline const char &at(std::size_t pos) const { return *(ptr_ + pos); }

  inline char *data();
  const char *data() const;

  std::vector<char> to_vector();

private:
  char *ptr_;
  std::size_t len_;
  LinearMemInfo mapped_;
};

struct RingBuffer {
  RingBuffer(std::size_t size, std::size_t low_watermark,
             std::size_t high_watermark);

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
  void memcpy_out(void *data, size_t sz);

  // non filled sequence for writes
  mutable_buffers_type prepared();
  mutable_buffers_type prepared(std::size_t max_size);

  const_buffers_type data() const;
  const_buffers_type data(std::size_t max_size) const;

  bool empty() const;
  std::size_t ready_size() const;
  std::size_t ready_write_size() const;
  bool below_watermark() const;
  bool below_high_watermark() const;

  template <typename Sink>
  friend void AbslStringify(Sink &sink, const RingBuffer &buffer) {
    absl::Format(&sink, "f:[(%zu, %zu)], n:[(%zu, %zu)]", buffer._filled_start,
                 buffer._filled_size, buffer._non_filled_start,
                 buffer._non_filled_size);
  }

  void check(int len, std::string_view method) const;
  inline char char_at(std::size_t pos) const;
  int peek_int() const;
  buffers_2<std::string_view> peek_string_view(int len) const;
  buffers_2<bytes_view> peek_span(int len) const;
  std::span<char> peek_linear_span(int len);
  std::size_t peek_pos() const;

private:
  LinnearArray _data;
  // std::vector<char> _data;
  std::size_t _size;
  std::size_t _filled_start;
  std::size_t _filled_size;
  std::size_t _non_filled_start;
  std::size_t _non_filled_size;
  DestructionSignaller _destruction_signaller{"RingBuffer"};
  std::size_t _low_watermark;
  std::size_t _high_watermark;
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

class infinite_timer {
public:
  static constexpr auto interval = asio::chrono::seconds(3);
  infinite_timer(asio::io_context &io_context);

private:
  void start();
  asio::steady_timer timer_;
};
} // namespace am
