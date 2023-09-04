#include "server-protocol.hpp"
#include "protocol.hpp"
#include <asio/buffer.hpp>
#include <cstring>
#include <filesystem>

void ServerEncoder::fill_time(std::string_view time, WriteBuffer &buff) {
  fill_envelope(Envelope{1, static_cast<int>(time.size())}, buff);
  buff.memcpy_in(time.data(), time.size());
}

void ServerEncoder::fill_mp3(mp3 &file, WriteBuffer &buff) {
  fill_envelope(Envelope{2, static_cast<int>(file.size())}, buff);
  // send file will send the rest
}
