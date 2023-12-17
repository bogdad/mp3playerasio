#pragma once

#include <absl/functional/any_invocable.h>
#include <asio.hpp>
#include <asio/ip/tcp.hpp>
#include <cstddef>
#include <cstdio>

#if defined(__LINUX__) || defined(__APPLE__)
#include <sys/types.h>
#elif defined(_WIN32) || defined(_WIN64)
#include <asio/windows/object_handle.hpp>
#include <minwinbase.h>
#endif

namespace am {

#if defined(__linux__) || defined(__APPLE__)
struct file_view {
  size_t _current;
  size_t _size;
  off_t len() const;
  void consume(size_t len);
};

struct SendFilePosix {};
#elif defined(_WIN32) || defined(_WIN64)
struct SendFileWin {
  SendFileWin() = default;
  SendFileWin(const SendFileWin &other) = delete;
  SendFileWin &operator=(const SendFileWin &other) = delete;
  SendFileWin(SendFileWin &&other) noexcept;
  SendFileWin &operator=(SendFileWin &&other) = delete;
  ~SendFileWin();

  OVERLAPPED overlapped_{};
  std::unique_ptr<asio::windows::object_handle> event_{};
};
#endif

struct SendFile;
using OnChunkSent =
    absl::AnyInvocable<void(std::size_t bytes_left, SendFile &inprogress)>;
struct SendFile {
  SendFile(asio::io_context &io_context, asio::ip::tcp::socket &socket,
           std::FILE *file, std::size_t size, OnChunkSent &&on_chunk_sent);
  void call();

private:
  asio::io_context &io_context_;
  asio::ip::tcp::socket &socket_;
  std::FILE *file_;
  std::size_t cur_;
  std::size_t size_;
  OnChunkSent on_chunk_sent_;
#if defined(__LINUX__) || defined(__APPLE__)
  SendFilePosix platform_{};
#elif defined(_WIN32) || defined(_WIN64)
  SendFileWin platform_{};
#endif
};

} // namespace am
