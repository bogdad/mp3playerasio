#include "protocol.hpp"
#include <absl/log/log.h>
#include <asio/buffer.hpp>
#include <cstring>
#include <exception>
#include <span>
#include <string_view>

void State::check(int len, std::string_view method) const {
  if (_curr + len >= _last_written) {
    LOG(ERROR) << "State cant " << method << ": bumped into buffer size "
               << _curr << " / " << cbuff().size();
    std::terminate();
  }
}

int State::peek_int() const {
  check(4, "peek_int");
  int ret;
  std::memcpy(&ret, cbuff().data() + _curr, sizeof(ret));
  return ret;
}

int State::nw() const { return _last_written - _curr; }

std::string_view State::peek_string_view(int len) const {
  check(len, "peek_string_view");
  return std::string_view(cbuff().data() + _curr, len);
}

bytes_view State::peek_span(int len) const {
  check(len, "peek_span");
  return cbuff().subspan(_curr, len);
}

void State::skip_len(int len) { _curr += len; }

asio::mutable_buffer State::next_buffer() {
  auto buf = buff();
  auto ret = asio::mutable_buffer((char *)buf.data() + _last_written,
                                  buf.size() - _last_written);
  return ret;
}

void State::advance_buffer(int len) { _last_written += len; }

std::span<char> State::buff() {
  if (_cur_buffer != 0)
    return _buff1;
  return _buff0;
}

std::span<const char> State::cbuff() const {
  if (_cur_buffer != 0)
    return _buff1;
  return _buff0;
}

bool Handler::try_read(State &state) {
  if (_state == HandlerState::before_envelope) {
    if (state.nw() >= sizeof(Envelope)) {
      // we can parse the envelope now
      _envelope.message_type = state.peek_int();
      _envelope.message_size = state.peek_int();
      _state = HandlerState::have_envelope;
      LOG(INFO) << "Handle::try_read state " << (int)_state;
      return try_read(state);
    } else {
      return false;
    }
  } else if (_state == HandlerState::have_envelope) {
    return true;
  } else {
    std::terminate();
  }
}

void Handler::reset() {
  _state = HandlerState::before_envelope;
  _envelope = {};
}
