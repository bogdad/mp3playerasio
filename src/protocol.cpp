#include "protocol.hpp"
#include <absl/log/log.h>
#include <asio/buffer.hpp>
#include <cstring>
#include <exception>
#include <span>
#include <string_view>

void ReadBuffer::check(int len, std::string_view method) const {
  if (_curr + len > _last_written) {
    LOG(ERROR) << "State cant " << method << ": bumped into buffer size "
               << _curr << " + " << len << " >= " << _last_written;
    std::terminate();
  }
}

int ReadBuffer::peek_int() const {
  check(4, "peek_int");
  int ret;
  std::memcpy(&ret, cbuff().data() + _curr, sizeof(ret));
  return ret;
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

void WriteBuffer::memcpy_in(const void *data, size_t sz) {
  if (_last_written + sz >= _arr.size())
    std::terminate();
  std::memcpy(_arr.data() + _last_written, data, sz);
  _last_written += sz;
}

asio::const_buffer WriteBuffer::as_buffer() const {
  return asio::buffer(_arr.data(), _last_written);
}

void Encoder::fill_envelope(Envelope envelope, WriteBuffer &buff) {
  buff.memcpy_in(&envelope, sizeof(envelope));
}
