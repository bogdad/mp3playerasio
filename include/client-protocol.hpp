#pragma once

#include "protocol.hpp"
#include <absl/functional/any_invocable.h>
#include <asio.hpp>
#include <exception>
#include <string>
#include <string_view>
#include <vector>

using mutable_buffers = std::vector<asio::mutable_buffer>;
using const_buffers = std::vector<asio::const_buffer>;

using bytes_view = std::span<const char>;

struct ClientDecoder : Decoder {
  ClientDecoder(absl::AnyInvocable<void(std::string_view ts)> &&on_time,
                absl::AnyInvocable<void(bytes_view ts)> &&on_mp3_bytes)
      : _on_time(std::move(on_time)), _on_mp3_bytes(std::move(on_mp3_bytes)) {}

  void try_read_client(ReadBuffer &state);

  absl::AnyInvocable<void(std::string_view)> _on_time;
  absl::AnyInvocable<void(bytes_view)> _on_mp3_bytes;
};
