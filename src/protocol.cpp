#include "protocol.hpp"
#include <absl/log/log.h>
#include <asio/buffer.hpp>
#include <cstring>
#include <exception>
#include <iterator>
#include <span>
#include <string>
#include <string_view>

RingBuffer::RingBuffer(std::size_t size): _size(size), _data(size) {
  _filled_start = 0;
  _filled_size = 0;
  _non_filled_start = 0;
  _non_filled_size = size;
}

void RingBuffer::reset() {
  _filled_start = 0;
  _filled_size = 0;
  _non_filled_start = 0;
  _non_filled_size = _size;
}

void RingBuffer::commit(std::size_t len) {
  _non_filled_size += len;
  _filled_size -= len;
  auto left_to_the_right = _size - _filled_start;
  if (_filled_size > left_to_the_right) {
    _filled_start = _filled_size - left_to_the_right;
  } else {
    _filled_start += len;
  }
}

void RingBuffer::consume(std::size_t len) {
  _filled_size += len;
  _non_filled_size -= len;
  auto left_to_the_right = _size - _non_filled_start;
  if (_non_filled_size > left_to_the_right) {
    _non_filled_start = _non_filled_size - left_to_the_right;
  } else {
    _non_filled_start += len;
  }
}

void RingBuffer::memcpy_in(void *data, int sz) {
  // std::memcpy(_arr.data() + _last_written, data, sz);
  auto left_to_the_right = _size - _non_filled_start;
  if (sz > left_to_the_right) {
    std::memcpy(&_data.at(_non_filled_start), data, left_to_the_right);
    std::memcpy(_data.data(), ((char*)data) + left_to_the_right, sz - left_to_the_right);
  } else {
    std::memcpy(&_data.at(_non_filled_start), data, sz);
  }
  consume(sz);
}

RingBuffer::const_buffers_type RingBuffer::data() const {
  if (_filled_size == 0) return const_buffers_type();
  auto left_to_the_right = _size - _filled_start;
  if (_filled_size > left_to_the_right) {
    return const_buffers_type(
      asio::const_buffer(&_data.at(_filled_start), left_to_the_right),
      asio::const_buffer(_data.data(), _filled_size-left_to_the_right));
  }
  return const_buffers_type(
    asio::const_buffer(&_data.at(_filled_start), _filled_start));
}

RingBuffer::const_buffers_type RingBuffer::data(std::size_t max_size) const {
  if (_filled_size == 0) return const_buffers_type();
  auto buf_size = std::min(_filled_size, max_size);
  auto left_to_the_right = _size - _filled_start;
  if (buf_size > left_to_the_right) {
    return const_buffers_type(
      asio::const_buffer(&_data.at(_filled_start), left_to_the_right),
      asio::const_buffer(_data.data(), buf_size-left_to_the_right));
  }
  return const_buffers_type(
    asio::const_buffer(&_data.at(_filled_start), buf_size));
}

RingBuffer::mutable_buffers_type RingBuffer::prepared() {
  if (_non_filled_size == 0) {
    return mutable_buffers_type();
  }
  auto left_to_the_right = _size - _non_filled_start;
  if (_non_filled_size > left_to_the_right) {
    // we have 2 parts
    return mutable_buffers_type(
      asio::mutable_buffer(&_data.at(_non_filled_start), left_to_the_right),
      asio::mutable_buffer(_data.data(), _non_filled_size - left_to_the_right));
  }
  return mutable_buffers_type(asio::mutable_buffer(&_data.at(_non_filled_start), _non_filled_size));
}

RingBuffer::mutable_buffers_type RingBuffer::prepared(std::size_t max_size) {
  if (_non_filled_size == 0) {
    return mutable_buffers_type();
  }
  auto buf_size = std::min(_non_filled_size, max_size);
  auto left_to_the_right = _size - _non_filled_start;
  if (buf_size > left_to_the_right) {
    return mutable_buffers_type(
      asio::mutable_buffer(&_data.at(_non_filled_start), left_to_the_right),
      asio::mutable_buffer(_data.data(), buf_size - left_to_the_right));
  }
  return mutable_buffers_type(
    asio::mutable_buffer(&_data.at(_non_filled_start), buf_size));
}

bool RingBuffer::empty() const {
  return _filled_size == 0;
}

void RingBuffer::check(int len, std::string_view method) const {
  if (len > _filled_size) {
    LOG(ERROR) << "RingBuffer " << method << ": cant read "
     << len << " >= " << _filled_size << " debug " << this;
    std::terminate();
  }
}

inline char RingBuffer::char_at(std::size_t pos) const {
  if (pos < _size) return _data.at(pos);
  return _data.at(pos - _size);
}

typedef union {
    char c[4];
    int i;
} raw_int;

int RingBuffer::peek_int() const {
  check(4, "peek_int");
  int ret;
  if (_filled_size + 4 < _size) {
    std::memcpy(&ret, &_data.at(_filled_start), sizeof(ret));
  } else {
    raw_int ri;
    ri.c[0] = char_at(_filled_start);
    ri.c[1] = char_at(_filled_start + 1);
    ri.c[2] = char_at(_filled_start + 2);
    ri.c[3] = char_at(_filled_start + 3);
    ret = ri.i;
  }
  LOG(INFO) << "peek_int: " << ret;
  return ret;
}

buffers_2<std::string_view> RingBuffer::peek_string_view(int len) const {
  check(len, "peek_string_view");
  auto left_to_the_right = _size - _filled_start;
  if (len > left_to_the_right) {
    return buffers_2(
      std::string_view(&_data.at(_filled_start), left_to_the_right),
      std::string_view(_data.data(), len - left_to_the_right));
  } else {
    return buffers_2(std::string_view(&_data.at(_filled_start), len));
  }
}

buffers_2<std::span<const char>> RingBuffer::peek_span(int len) const {
  check(len, "peek_span");
  auto left_to_the_right = _size - _filled_start;
  if (len > left_to_the_right) {
    return buffers_2(std::span(&_data.at(_filled_start), left_to_the_right),
      std::span(_data.data(), len - left_to_the_right));
  } else {
    return buffers_2(std::span(&_data.at(_filled_start), len));
  }
}

int ReadBuffer::nw() const { return _last_written - _curr; }

std::string_view ReadBuffer::peek_string_view(int len) const {
  check(len, "peek_string_view");
  return std::string_view(cbuff().data() + _curr, len);
}

bytes_view ReadBuffer::peek_span(int len) const {
  check(len, "peek_span");
  return cbuff().subspan(_curr, len);
}

void ReadBuffer::skip_len(int len) { _curr += len; }

asio::mutable_buffer ReadBuffer::next_buffer() {
  auto buf = buff();
  if (buf.size() == _last_written) {
    LOG(ERROR) << "buffer exhausted " << buf.size() << " / " << _last_written;
    std::terminate();
  }
  auto ret = asio::mutable_buffer((char *)buf.data() + _last_written,
                                  buf.size() - _last_written);
  return ret;
}

void ReadBuffer::advance_buffer(int len) { _last_written += len; }

std::span<char> ReadBuffer::buff() {
  if (_cur_buffer != 0)
    return _buff1;
  return _buff0;
}

std::span<const char> ReadBuffer::cbuff() const {
  if (_cur_buffer != 0)
    return _buff1;
  return _buff0;
}

void Envelope::log() {
  LOG(INFO) << "envelope message type " << message_type << " message size " << message_size;
}

bool Decoder::try_read(ReadBuffer &state) {
  if (_state == DecoderState::before_envelope) {
    if (state.nw() >= sizeof(Envelope)) {
      // we can parse the envelope now
      _envelope.message_type = state.peek_int();
      state.skip_len(4);
      _envelope.message_size = state.peek_int();
      state.skip_len(4);
      _state = DecoderState::have_envelope;
      LOG(INFO) << "Handle::try_read state " << (int)_state
                << " envelope, message_size " << _envelope.message_size
                << " message_type " << _envelope.message_type;
      return try_read(state);
    } else {
      return false;
    }
  } else if (_state == DecoderState::have_envelope) {
    return true;
  } else {
    std::terminate();
  }
}

void Decoder::reset() {
  _state = DecoderState::before_envelope;
  _envelope = {};
}

void Encoder::fill_envelope(Envelope envelope, RingBuffer &buff) {
  buff.memcpy_in(&envelope, sizeof(envelope));
}
