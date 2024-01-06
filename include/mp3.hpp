#pragma once

#include "mp3-system.hpp"
#include <absl/functional/any_invocable.h>
#include <absl/log/log.h>
#include <asio.hpp>
#include <asio/error_code.hpp>
#include <asio/io_context.hpp>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>

namespace am {

namespace fs = std::filesystem;

struct file_deleter {
  void operator()(std::FILE *fp) { std::fclose(fp); }
};

using fhandle = std::unique_ptr<std::FILE, file_deleter>;

struct Mp3 {

  static Mp3 create(fs::path filepath);
  std::size_t size() const;
  bool send(asio::io_context &io_context, const asio::ip::tcp::socket &socket,
            OnChunkSent &&on_chunk_sent);
  void cancel();
private:
  Mp3(fhandle fd, size_t size)
      : fd_(std::move(fd))
      , size_(size){};

  fhandle fd_;
  std::size_t size_;
  bool _started{false};
  std::optional<SendFile> send_file_{};
};

} // namespace am
