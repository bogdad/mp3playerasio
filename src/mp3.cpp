#include "mp3.hpp"
#include <absl/base/macros.h>
#include <absl/log/log.h>
#include <asio.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <cstddef>
#include <cstdio>
#include <exception>

namespace am {

Mp3 Mp3::create(fs::path filepath) {
  LOG(INFO) << "filepath " << filepath;
  if (!std::filesystem::exists(filepath)) {
    LOG(ERROR) << "file " << filepath << " does not exist";
    std::terminate();
  }
  auto sz = std::filesystem::file_size(filepath);
  fhandle f{std::fopen(filepath.string().c_str(), "rb")};

  return {std::move(f), sz};
}

bool Mp3::send(asio::io_context &io_context,
               const asio::ip::tcp::socket &socket,
               OnChunkSent &&on_chunk_sent) {
  auto &non_const_socket = const_cast<asio::ip::tcp::socket &>(socket);
  send_file_.emplace(io_context, non_const_socket, _fd.get(), _size,
                     std::move(on_chunk_sent));
  return true;
}

size_t Mp3::size() const { return _size; }

off_t file_view::len() const { return _size - _current; }

void file_view::consume(size_t l) {
  if (l > len())
    std::terminate();
  _current += l;
}

} // namespace am
