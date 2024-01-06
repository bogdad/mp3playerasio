#include "mp3-system.hpp"
#include <absl/log/log.h>
#include <asio/error.hpp>
#include <asio/error_code.hpp>
#include <asio/io_context.hpp>
#include <cstddef>
#include <exception>

#if defined(__linux__)
#  include <sys/sendfile.h>
#elif defined(__linux__) || defined(__APPLE__)
#  include <sys/_types/_off_t.h>
#elif defined(_WIN32) || defined(_WIN64)
#  include <handleapi.h>
#  include <io.h>
constexpr std::size_t TRANSMITFILE_MAX{(unsigned int)(2 << 30) - 1};
#endif

namespace am {

#if defined(__linux__) || defined(__APPLE__)
#elif defined(_WIN32) || defined(_WIN64)
SendFileWin::SendFileWin(SendFileWin &&other) noexcept
    : overlapped_(other.overlapped_)
    , event_(std::move(other.event_)) {
  other.event_ = nullptr;
  other.overlapped_.hEvent = nullptr;
}

SendFileWin::~SendFileWin() {
  if (event_)
    event_->cancel();
  if (overlapped_.hEvent != nullptr) {
    CloseHandle(overlapped_.hEvent);
  }
}
#endif

SendFile::SendFile(asio::io_context &io_context, asio::ip::tcp::socket &socket,
                   std::FILE *file, std::size_t size,
                   OnChunkSent &&on_chunk_sent)
    : io_context_(io_context)
    , socket_(socket)
    , file_(file)
    , cur_(0)
    , size_(size)
    , on_chunk_sent_(std::move(on_chunk_sent)) {
  call();
}

void SendFile::call() {
#if defined(__linux__) || defined(__APPLE__)
  std::size_t len = size_ - cur_;
  off_t res_len;
#if defined(__APPLE__)
  int res = sendfile(fileno(file_), socket_.lowest_layer().native_handle(),
                     cur_, &res_len, nullptr, 0);

#elif defined(__linux__)
  auto res = sendfile(socket_.lowest_layer().native_handle(), fileno(file_),
                     nullptr, len);
  if (res > 0) {
  	res_len = res;
	res = 0;
  } else {
        res_len=-1;
  }
#else
  static_assert(false);
#endif
  LOG(INFO) << "sent " << res_len;
  if (res == 0) {
    cur_ += res_len;
    on_chunk_sent_(size_ - cur_, *this);
  } else {
    int err = errno;
    if (err == EAGAIN) {
      cur_ += res_len;
      socket_.async_wait(asio::ip::tcp::socket::wait_write,
                         [this](const asio::error_code &ec) {
                           if (ec == asio::error::operation_aborted) {
                             return;
                           }
                           on_chunk_sent_(size_ - cur_, *this);
                         });
      LOG(INFO) << "sendfile: would block, EAGAIN";
    } else if (err == 104) {
      LOG(INFO) << "sendfile: client conection problem";
      on_chunk_sent_(0, *this);
    } else {
      LOG(ERROR) << "sendfile failed " << res << " errno " << err;
      std::terminate();
    }
  }
#elif defined(_WIN32) || defined(_WIN64)
  platform_.overlapped_ = {};
  DWORD bytes = std::min(size_ - cur_, TRANSMITFILE_MAX);
  auto socket = socket_.lowest_layer().native_handle();
  if (platform_.overlapped_.hEvent != nullptr) {
    CloseHandle(platform_.overlapped_.hEvent);
    platform_.overlapped_.hEvent = nullptr;
  }
  platform_.overlapped_.hEvent = CreateEvent(nullptr, true, false, nullptr);
  if (!platform_.overlapped_.hEvent) {
    std::terminate();
  }

  platform_.event_.reset(new asio::windows::object_handle(
      io_context_, platform_.overlapped_.hEvent));
  platform_.event_->async_wait([this](const asio::error_code &error) {
    if (error == asio::error::operation_aborted) {
      return;
    }
    std::size_t bytes_written = platform_.overlapped_.InternalHigh;
    cur_ += bytes_written;
    if (cur_ < size_) {
      on_chunk_sent_(size_ - cur_, *this);
    }
  });
  platform_.overlapped_.Offset = cur_ & 0x00000000FFFFFFFF;
  ;
  platform_.overlapped_.OffsetHigh = (cur_ & 0xFFFFFFFF00000000) >> 32;

  auto fhandle = (HANDLE)_get_osfhandle(_fileno(file_));

  if (!TransmitFile(socket, fhandle, size_ - cur_, bytes,
                    &platform_.overlapped_, nullptr, 0)) {
    auto err = GetLastError();
    auto wsaerr = WSAGetLastError();
    if ((err != ERROR_IO_PENDING) && (wsaerr != WSA_IO_PENDING)) {
      LOG(ERROR) << "sendfile failed ";
      std::terminate();
    }
  }
#endif
}

} // namespace am
