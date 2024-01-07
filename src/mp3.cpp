#include "mp3.hpp"
#include <absl/base/macros.h>
#include <absl/log/log.h>
#include <asio.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <optional>

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
  send_file_ = std::make_unique<SendFile>(io_context, non_const_socket, fd_.get(), size_,
                     std::move(on_chunk_sent));
  return true;
}

size_t Mp3::size() const { return size_; }

void Mp3::cancel() { if (send_file_) { 
  LOG(INFO) << "Mp3::cancel send file reset";
  send_file_.reset();
} }

void Mp3::precancel() { if (send_file_) { 
  LOG(INFO) << "Mp3::cancel send file reset";
  send_file_->cancel();
} }

} // namespace am
