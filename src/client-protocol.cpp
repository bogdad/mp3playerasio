#include "client-protocol.hpp"
#include "protocol.hpp"
#include <absl/log/log.h>

namespace am {

void ClientDecoder::try_read_client(RingBuffer& state)
{
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
      auto const read_size = std::min(static_cast<int>(state.ready_size()),
          _envelope.message_size);
      _on_mp3_bytes(state);
      if (state.ready_size() >= _envelope.message_size) {
        // finished the file
        reset();
        try_read_client(state);
      }
    }
  }
}

void ClientEncoder::fill_message(std::string_view msg, RingBuffer& buff)
{
  fill_envelope(Envelope { 3, static_cast<int>(msg.size()) }, buff);
  buff.memcpy_in(static_cast<char const*>(msg.data()), msg.size());
}
} // namespace am
