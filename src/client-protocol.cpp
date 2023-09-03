#include "client-protocol.hpp"

void ClientHandler::try_read_client(State &state) {
  if (try_read(state)) {
    if (_envelope.message_type == 1) {
      // get time
      if (state.nw() >= _envelope.message_size) {
        // got time
        _on_time(state.peek_string_view(_envelope.message_size));
        state.skip_len(_envelope.message_size);
        reset();
        try_read_client(state);
      }
    } else if (_envelope.message_type == 2) {
      if (_song_handler_state == ClientHandlerState::before_envelope) {
        if (state.nw() >= sizeof(SongEnvelope)) {
          _song_envelope.song_size = state.peek_int();
          state.skip_len(4);
          _song_envelope.chunks_left = state.peek_int();
          state.skip_len(4);
          _song_handler_state = ClientHandlerState::have_envelope;
          try_read_client(state);
        } else {
          // wait
        }
      } else if (_song_handler_state == ClientHandlerState::have_envelope) {
        if (state.nw() >= _song_envelope.song_size) {
          _on_mp3_bytes(state.peek_span(_song_envelope.song_size));
          state.skip_len(_song_envelope.song_size);
          reset();
          try_read_client(state);
        }
      }
    }
  }
}
