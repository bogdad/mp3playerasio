#pragma once

#include <asio.hpp>
#include <absl/functional/any_invocable.h>
#include <exception>
#include <string>
#include <string_view>
#include <vector>
#include "protocol.hpp"

using mutable_buffers = std::vector<asio::mutable_buffer>;
using const_buffers = std::vector<asio::const_buffer>;

using bytes_view = std::string_view;


struct SongEnvelope {
	int song_size;
	int chunks_left;
};


enum class ClientHandlerState {
	before_envelope,
	have_envelope
};

struct ClientHandler: public Handler {
	ClientHandler(
		absl::AnyInvocable<void(std::string_view ts)>&& on_time,
		absl::AnyInvocable<void(bytes_view ts)>&& on_mp3_bytes)
	: _on_time(std::move(on_time)), _on_mp3_bytes(std::move(on_mp3_bytes)){}

	void try_read_client(State &state) {
		if (try_read(state)) {
			if (_envelope.message_type == 1) {
				// get time
				if (state.nw() >= _envelope.message_size) {
					// got time
					_on_time(state.peek_string_view());
					state.skip_message_size();
					state.reset();
					try_read_client(state);
				}
			} else if (_envelope.message_type == 2) {
				if (_song_handler_state == ClientHandlerState::before_envelope) {
					if (state.nw() >= sizeof(SongEnvelope)) {
						_song_envelope.song_size = state.advance_int();
						_song_envelope.chunks_left = state.advance_int();
						_song_handler_state = ClientHandlerState::have_envelope;
						try_read_client(state);
					} else {
						// wait
					}
				} else if (_song_handler_state == ClientHandlerState::have_envelope) {
					if (state.nw() >= _song_envelope.song_size) {
						_on_mp3_bytes(state.peek_span(_song_envelope.song_size));
						state.skip_message_size();
						state.reset();
						try_read_client(state);
					}
				}
			}
		}
	}

	absl::AnyInvocable<void(std::string_view)> _on_time;
	ClientHandlerState _song_handler_state {};
	SongEnvelope _song_envelope {};
	absl::AnyInvocable<void(bytes_view)> _on_mp3_bytes;
};
