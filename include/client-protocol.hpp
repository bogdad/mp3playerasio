#pragma once

#include "protocol.hpp"
#include <absl/functional/any_invocable.h>
#include <asio.hpp>
#include <exception>
#include <string>
#include <string_view>
#include <vector>

namespace am {

using mutable_buffers = std::vector<asio::mutable_buffer>;
using const_buffers = std::vector<asio::const_buffer>;

using bytes_view = std::span<const char>;

struct ClientDecoder : Decoder {
  ClientDecoder(absl::AnyInvocable<void(buffers_2<std::string_view>)> &&on_time,
                absl::AnyInvocable<void(RingBuffer&)> &&on_mp3_bytes)
      : _on_time(std::move(on_time)), _on_mp3_bytes(std::move(on_mp3_bytes)) {}

  void try_read_client(RingBuffer &state);

  absl::AnyInvocable<void(buffers_2<std::string_view>)> _on_time;
  absl::AnyInvocable<void(RingBuffer&)> _on_mp3_bytes;
};

struct ClientEncoder : Encoder {
  void fill_message(std::string_view msg, RingBuffer &buff);
};

} // namespace am
