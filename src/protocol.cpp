#include "protocol.hpp"
#include <absl/log/log.h>
#include <cstring>
#include <exception>
#include <string_view>

void State::check(int len, std::string_view method) const {
  if (_curr + len >= _last_written) {
    LOG(ERROR) << "State cant " << method << ": bumped into buffer size "
               << _curr << " / " << _buff.size();
    std::terminate();
  }
}

int State::peek_int() const {
  check(4, "peek_int");
  int ret;
  std::memcpy(&ret, _buff.data() + _curr, sizeof(ret));
  return ret;
}

int State::nw() const { return _last_written - _curr; }

std::string_view State::peek_string_view(int len) const {
  check(len, "peek_string_view");
  return std::string_view(_buff.data() + _curr, len);
}

bytes_view State::peek_span(int len) const {
  check(len, "peek_span");
  return peek_string_view(len);
}

void State::skip_len(int len) { _curr += len; }

bool Handler::try_read(State &state) {
  if (_state == HandlerState::before_envelope) {
    if (state.nw() >= sizeof(Envelope)) {
      // we can parse the envelope now
      _envelope.message_type = state.peek_int();
      _envelope.message_size = state.peek_int();
      _state = HandlerState::have_envelope;
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
