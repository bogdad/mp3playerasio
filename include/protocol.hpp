#pragma once

#include <absl/base/macros.h>
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
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <sys/mman.h>

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
using on_commit_func = absl::AnyInvocable<void()>;

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
    if (_buffer_count == 0)
      return 0;
    auto res = size_t(_buffers[0].size());
    if (_buffer_count == 2)
      res += _buffers[1].size();
    return res;
  }

  inline std::byte &operator[](size_t pos) const {
    auto b0size = _buffers[0].size();
    if (pos < b0size) {
      return _buffers[0][pos];
    }
    return _buffers[1][pos - b0size];
  }

  inline std::size_t count() const { return _buffer_count; }

private:
  std::array<Buffer, 2> _buffers;
  std::size_t _buffer_count;
};

struct LinnearArray {
  LinnearArray(std::size_t size)
      : _ptr(nullptr), _p1(nullptr), _p2(nullptr), _len(0), _shname("") {
    int res = init(size);
    if (res == -1) {
      std::terminate();
    }
    _ptr = _p1;
  }
  ~LinnearArray() {
    if (_p1)
      munmap(_p1, _len);
    if (_p2)
      munmap(_p2, _len);
    if (!_shname.empty())
      shm_unlink(_shname.c_str());
  }
  LinnearArray(const LinnearArray &other) = delete;
  LinnearArray &operator=(const LinnearArray &other) = delete;

  std::size_t size() { return _len; }

  inline char &at(std::size_t pos) { return *(_ptr + pos); }

  inline const char &at(std::size_t pos) const { return *(_ptr + pos); }

  inline char *data() { return _ptr; }

  const char *data() const { return _ptr; }

  std::vector<char> to_vector() {
    auto res = std::vector<char>(size());
    memcpy(res.data(), data(), size());
    return res;
  }

private:
  int init(std::size_t minsize) {
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
    void *p =
        ::mmap(nullptr, 2 * len, PROT_WRITE, MAP_ANON | MAP_SHARED, -1, 0);
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
    printf("pointer %p %s: %p %p %p %ld %c %c\n", this, _shname.c_str(), p, _p1,
           _p2, (char *)_p2 - (char *)_p1, _p1[0], _p2[0]);
    _len = len;
    _p1[0] = 0;
    return 0;
  }

  char *_ptr;
  char *_p1;
  char *_p2;
  std::size_t _len;
  std::string _shname;
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

  void enqueue_on_commit_func(on_commit_func &&func) noexcept;

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

private:
  LinnearArray _data;
  // std::vector<char> _data;
  std::size_t _size;
  std::size_t _filled_start;
  std::size_t _filled_size;
  std::size_t _non_filled_start;
  std::size_t _non_filled_size;
  std::vector<on_commit_func> _on_commit_funcs;
  DestructionSignaller _destruction_signaller {"RingBuffer"};
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
  infinite_timer(asio::io_context &io_context) : timer_(io_context, interval) {
    start();
  }

private:
  void start();
  asio::steady_timer timer_;
};
} // namespace am
