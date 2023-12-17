#include "mp3-system.hpp"
#include <absl/log/log.h>
#include <asio/error.hpp>
#include <asio/io_context.hpp>
#include <cstddef>
#include <cstdint>
#include <exception>

#if defined (__LINUX__) || defined(__APPLE__)

#elif defined (_WIN32) || defined (_WIN64)
#  include <handleapi.h>
#include <io.h>
constexpr std::size_t TRANSMITFILE_MAX{(unsigned int)(2 << 30) - 1};
#endif

namespace am {

SendFile::SendFile(asio::io_context &io_context, asio::ip::tcp::socket &socket, std::FILE *file, std::size_t size) 
  :io_context_(io_context), socket_(socket), file_(file), cur_(0), size_(size) {
  #if defined(__linux__) || defined(__APPLE__)  
  call();
  #elif defined (_WIN32) || defined (_WIN64)
  overlapped_ = {};
  overlapped_.hEvent = nullptr;
  call();
  #endif
}

void SendFile::call() {
  #if defined(__linux__) || defined(__APPLE__)
  len = _current.len();
  auto &non_const_socket = const_cast<asio::ip::tcp::socket &>(socket);
  int res = sendfile(fileno(_fd.get()),
                     non_const_socket.lowest_layer().native_handle(),
                     _current._current, &len, nullptr, 0);
  LOG(INFO) << "sent " << len;
  if (res == 0) {
    _current.consume(len);
  } else {
    int err = errno;
    if (err == EAGAIN) {
      _current.consume(len);
      return 0;
    } else {
      LOG(ERROR) << "sendfile failed " << res << " errno " << err;
      std::terminate();
    }
  }  
  #elif defined (_WIN32) || defined (_WIN64)
  overlapped_ = {};
  DWORD bytes = std::min(size_-cur_, TRANSMITFILE_MAX);
  auto socket = socket_.lowest_layer().native_handle();
  if (overlapped_.hEvent != nullptr) {
    CloseHandle(overlapped_.hEvent);
    overlapped_.hEvent = nullptr;
  }
  overlapped_.hEvent = CreateEvent(nullptr, true, true, nullptr);
  if (!overlapped_.hEvent) {
    std::terminate();
  }

  event_.reset(new asio::windows::object_handle(io_context_, overlapped_.hEvent));
  event_->async_wait([this](const asio::error_code &error){
    if (error == asio::error::operation_aborted) {
      return;
    }
    std::size_t bytes_written = overlapped_.InternalHigh;
    cur_ += bytes_written;
    if (cur_ < size_) {
      call();
    }
  });
  overlapped_.Offset = cur_ & 0x00000000FFFFFFFF;;
  overlapped_.OffsetHigh = (cur_ & 0xFFFFFFFF00000000) >> 32;

  auto fhandle = (HANDLE)_get_osfhandle(_fileno(file_));

  if (!TransmitFile(socket, fhandle, bytes, 0, &overlapped_, nullptr, 0)) {
    auto err = GetLastError();
    auto wsaerr = WSAGetLastError();
    if ((err != ERROR_IO_PENDING) && (wsaerr != WSA_IO_PENDING)) {
      LOG(ERROR) << "sendfile failed ";
      std::terminate();
    }
  }
  #endif
}

SendFile::~SendFile() {
  #if defined(__linux__) || defined(__APPLE__)
  #elif defined (_WIN32) || defined (_WIN64)
  event_->cancel();
  if (overlapped_.hEvent != nullptr) {
    CloseHandle(overlapped_.hEvent);
  }
  #endif
}

} // namespace am
