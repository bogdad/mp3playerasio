#include "server-protocol.hpp"
#include "protocol.hpp"
#include <absl/log/log.h>
#include <asio/buffer.hpp>

namespace am {

void ServerDecoder::try_read_server(RingBuffer &state) {
  if (try_read(state)) {
    if (_envelope.message_type == 3) {
      // get time
      if (state.ready_size() >= _envelope.message_size) {
        // got time
        on_message_(state.peek_string_view(_envelope.message_size));
        state.commit(_envelope.message_size);
        reset();
        try_read_server(state);
      }
    }
  }
}

void ServerEncoder::fill_time(std::string_view time, RingBuffer &buff) {
  fill_envelope(Envelope{1, static_cast<int>(time.size())}, buff);
  buff.memcpy_in(static_cast<const char *>(time.data()), time.size());
}

void ServerEncoder::fill_mp3(Mp3 &file, RingBuffer &buff) {
  fill_envelope(Envelope{2, static_cast<int>(file.size())}, buff);
  // send file will send the rest
}

} // namespace am
