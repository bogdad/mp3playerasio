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
	int nw() const;
	int peek_int() const;
	std::string_view peek_string_view(int len) const;
	bytes_view peek_span(int len) const;

	void skip_len(int len);

	void check(int len, std::string_view method) const;

	std::array<char, 1024> _buff;
	int _curr;
	int _last_written;
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

  bool try_read(State &state);
  void reset();
};
