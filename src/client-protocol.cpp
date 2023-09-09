#include "client-protocol.hpp"
#include "protocol.hpp"
#include <absl/log/log.h>

namespace am {

void ClientDecoder::try_read_client(RingBuffer &state) {
  if (try_read(state)) {
    //_envelope.log();
    if (_envelope.message_type == 1) {
      // get time
      if (state.ready_size() >= _envelope.message_size) {
        // got time
        _on_time(state.peek_string_view(_envelope.message_size));
        state.commit(_envelope.message_size);
        reset();
        try_read_client(state);
      }
    } else if (_envelope.message_type == 2) {
      if (state.ready_size() >= _envelope.message_size) {
        _on_mp3_bytes(state.peek_span(_envelope.message_size));
        state.commit(_envelope.message_size);
        reset();
        try_read_client(state);
      }
    }
  }
}

void ClientEncoder::fill_message(std::string_view msg, RingBuffer &buff) {
  fill_envelope(Envelope{3, static_cast<int>(msg.size())}, buff);
  buff.memcpy_in((char *)msg.data(), msg.size());
}
} // namespace am
