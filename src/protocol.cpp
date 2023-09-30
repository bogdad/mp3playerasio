#include "protocol.hpp"
#include <absl/log/log.h>
#include <asio/buffer.hpp>
#include <cstddef>
#include <cstring>
#include <exception>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace am {

RingBuffer::RingBuffer(std::size_t size)
    : _data(size), _size(_data.size()), _filled_start(0), _filled_size(0),
      _non_filled_start(0), _non_filled_size(_data.size()) {}

void RingBuffer::reset() {
  _filled_start = 0;
  _filled_size = 0;
  _non_filled_start = 0;
  _non_filled_size = _size;
}

void RingBuffer::commit(std::size_t len) {
  _non_filled_size += len;
  _filled_size -= len;
  _filled_start += len;
  _filled_start %= _size;
}

void RingBuffer::consume(std::size_t len) {
  _filled_size += len;
  _non_filled_size -= len;
  _non_filled_start += len;
  _non_filled_start %= _size;
}

void RingBuffer::memcpy_in(const void *data, size_t sz) {
  // std::memcpy(_arr.data() + _last_written, data, sz);
  auto left_to_the_right = _size - _non_filled_start;
  if (sz > left_to_the_right) {
    std::memcpy(&_data.at(_non_filled_start), data, left_to_the_right);
    std::memcpy(_data.data(),
                static_cast<const char *>(data) + left_to_the_right,
                sz - left_to_the_right);
  } else {
    std::memcpy(&_data.at(_non_filled_start), data, sz);
  }
  consume(sz);
}

RingBuffer::const_buffers_type RingBuffer::data() const {
  if (_filled_size == 0)
    return {};
  auto left_to_the_right = _size - _filled_start;
  if (_filled_size > left_to_the_right) {
    return {asio::const_buffer(&_data.at(_filled_start), left_to_the_right),
            asio::const_buffer(_data.data(), _filled_size - left_to_the_right)};
  }
  return const_buffers_type(
      asio::const_buffer(&_data.at(_filled_start), _filled_size));
}

RingBuffer::const_buffers_type RingBuffer::data(std::size_t max_size) const {
  if (_filled_size == 0)
    return {};
  auto buf_size = std::min(_filled_size, max_size);
  auto left_to_the_right = _size - _filled_start;
  if (buf_size > left_to_the_right) {
    return {asio::const_buffer(&_data.at(_filled_start), left_to_the_right),
            asio::const_buffer(_data.data(), buf_size - left_to_the_right)};
  }
  return const_buffers_type(
      asio::const_buffer(&_data.at(_filled_start), buf_size));
}

RingBuffer::mutable_buffers_type RingBuffer::prepared() {
  if (_non_filled_size == 0) {
    return {};
  }
  auto left_to_the_right = _size - _non_filled_start;
  if (_non_filled_size > left_to_the_right) {
    // we have 2 parts
    return {
        asio::mutable_buffer(&_data.at(_non_filled_start), left_to_the_right),
        asio::mutable_buffer(_data.data(),
                             _non_filled_size - left_to_the_right)};
  }
  return mutable_buffers_type(
      asio::mutable_buffer(&_data.at(_non_filled_start), _non_filled_size));
}

RingBuffer::mutable_buffers_type RingBuffer::prepared(std::size_t max_size) {
  if (_non_filled_size == 0) {
    return {};
  }
  auto buf_size = std::min(_non_filled_size, max_size);
  auto left_to_the_right = _size - _non_filled_start;
  if (buf_size > left_to_the_right) {
    return {
        asio::mutable_buffer(&_data.at(_non_filled_start), left_to_the_right),
        asio::mutable_buffer(_data.data(), buf_size - left_to_the_right)};
  }
  return mutable_buffers_type(
      asio::mutable_buffer(&_data.at(_non_filled_start), buf_size));
}

bool RingBuffer::empty() const { return _filled_size == 0; }

std::size_t RingBuffer::ready_size() const { return _filled_size; }

void RingBuffer::check(int len, std::string_view method) const {
  if (len > _filled_size) {
    LOG(ERROR) << "RingBuffer " << method << ": cant read " << len
               << " >= " << _filled_size << " debug " << this;
    std::terminate();
  }
}

inline char RingBuffer::char_at(std::size_t pos) const {
  if (pos < _size)
    return _data.at(pos);
  return _data.at(pos - _size);
}

using raw_int = union {
  std::array<char, 4> c;
  int i;
};

int RingBuffer::peek_int() const {
  check(4, "peek_int");
  int ret = 0;
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
    return {std::string_view(&_data.at(_filled_start), left_to_the_right),
            std::string_view(_data.data(), len - left_to_the_right)};
  } else {
    return buffers_2(std::string_view(&_data.at(_filled_start), len));
  }
}

buffers_2<std::span<const char>> RingBuffer::peek_span(int len) const {
  check(len, "peek_span");
  auto left_to_the_right = _size - _filled_start;
  if (len > left_to_the_right) {
    return {std::span(&_data.at(_filled_start), left_to_the_right),
            std::span(_data.data(), len - left_to_the_right)};
  } else {
    return buffers_2(std::span(&_data.at(_filled_start), len));
  }
}

std::span<char> RingBuffer::peek_linear_span(int len) {
  check(len, "peek_linear_span");
  static_assert(std::same_as<LinnearArray, decltype(_data)>, "_data should be linear array, to support liear view");
  return {_data.data(), static_cast<std::size_t>(len)};
}

void Envelope::log() {
  LOG(INFO) << "envelope message type " << message_type << " message size "
            << message_size;
}

bool Decoder::try_read(RingBuffer &state) {
  if (_state == DecoderState::before_envelope) {
    if (state.ready_size() >= sizeof(Envelope)) {
      // we can parse the envelope now
      _envelope.message_type = state.peek_int();
      state.commit(4);
      _envelope.message_size = state.peek_int();
      state.commit(4);
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

} // namespace am
