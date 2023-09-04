#include "client-protocol.hpp"

void ClientDecoder::try_read_client(ReadBuffer &state) {
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
      if (state.nw() >= _envelope.message_size) {
        _on_mp3_bytes(state.peek_span(_envelope.message_size));
        state.skip_len(_envelope.message_size);
        reset();
        try_read_client(state);
      }
    }
  }
}
