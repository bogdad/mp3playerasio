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

struct ClientHandler : Handler {
  ClientHandler(absl::AnyInvocable<void(std::string_view ts)> &&on_time,
                absl::AnyInvocable<void(bytes_view ts)> &&on_mp3_bytes)
      : _on_time(std::move(on_time)), _on_mp3_bytes(std::move(on_mp3_bytes)) {}

  void try_read_client(State &state);
  void client_reset();

  absl::AnyInvocable<void(std::string_view)> _on_time;
  ClientHandlerState _song_handler_state{};
  SongEnvelope _song_envelope{};
  absl::AnyInvocable<void(bytes_view)> _on_mp3_bytes;
};
