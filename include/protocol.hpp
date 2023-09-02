#pragma once

#include <asio.hpp>
#include <absl/functional/any_invocable.h>
#include <exception>
#include <string>
#include <string_view>
#include <vector>

using mutable_buffers = std::vector<asio::mutable_buffer>;
using const_buffers = std::vector<asio::const_buffer>;

using bytes_view = std::string_view;

struct State {
	std::array<char, 1024> buff;
	int curr;
	int committed;
	int nw();
	int advance_int();
	std::string_view peek_string_view();
	bytes_view peek_span(int len);
	void skip_message_size();
	void reset();
};

struct Envelope {
	int message_type;
	int message_size;
};

enum class HandlerState {
	before_envelope,
	have_envelope
};

struct Handler {
  HandlerState _state;
  Envelope _envelope;

  bool try_read(State &state) {
  	if (_state == HandlerState::before_envelope) {
  		if (state.nw() >= sizeof(Envelope)) {
  			// we can parse the envelope now
  			_envelope.message_type = state.advance_int();
  			_envelope.message_size = state.advance_int();
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
};
