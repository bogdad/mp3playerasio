#pragma once

#include "mp3.hpp"
#include "protocol.hpp"
#include <asio.hpp>
#include <asio/buffer.hpp>

namespace am {

struct ServerDecoder : Decoder {
  ServerDecoder(
      absl::AnyInvocable<void(buffers_2<std::string_view>)> on_message)
      : _on_message(std::move(on_message)) {}

  void try_read_server(RingBuffer &state);
  absl::AnyInvocable<void(buffers_2<std::string_view>)> _on_message;
};

struct ServerEncoder : Encoder {

  void fill_time(std::string_view time, RingBuffer &buf);
  void fill_mp3(Mp3 &file, RingBuffer &buff);
};
} // namespace am
