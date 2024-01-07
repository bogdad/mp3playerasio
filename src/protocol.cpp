#include "protocol.hpp"
#include "protocol-system.hpp"

#include <absl/log/log.h>
#include <asio/buffer.hpp>
#include <cstddef>
#include <cstring>
#include <exception>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace am {

LinnearArray::LinnearArray(std::size_t size)
    : ptr_(nullptr)
    , len_(0)
    , mapped_(size) {
  len_ = mapped_.len_;
  ptr_ = mapped_.p1_;
}

std::size_t LinnearArray::size() const { return len_; }

inline char *LinnearArray::data() { return ptr_; }

const char *LinnearArray::data() const { return ptr_; }

std::vector<char> LinnearArray::to_vector() {
  auto res = std::vector<char>(size());
  memcpy(res.data(), data(), size());
  return res;
}

RingBuffer::RingBuffer(std::size_t size, std::size_t low_watermark,
                       std::size_t high_watermark)
    : _data(size)
    , _size(_data.size())
    , filled_start_(0)
    , filled_size_(0)
    , non_filled_start_(0)
    , non_filled_size_(_data.size())
    , _low_watermark(low_watermark)
    , _high_watermark(high_watermark)
    , on_commit_([](){})
    {}

void RingBuffer::reset() {
  filled_start_ = 0;
  filled_size_ = 0;
  non_filled_start_ = 0;
  non_filled_size_ = _size;
}

void RingBuffer::commit(std::size_t len) {
  non_filled_size_ += len;
  filled_size_ -= len;
  filled_start_ += len;
  filled_start_ %= _size;
  on_commit_();
}

void RingBuffer::consume(std::size_t len) {
  filled_size_ += len;
  non_filled_size_ -= len;
  non_filled_start_ += len;
  non_filled_start_ %= _size;
}

void RingBuffer::memcpy_in(const void *data, size_t sz) {
  auto left_to_the_right = _size - non_filled_start_;
  if (sz > left_to_the_right) {
    std::memcpy(&_data.at(non_filled_start_), data, left_to_the_right);
    std::memcpy(_data.data(),
                static_cast<const char *>(data) + left_to_the_right,
                sz - left_to_the_right);
  } else {
    std::memcpy(&_data.at(non_filled_start_), data, sz);
  }
  consume(sz);
}

void RingBuffer::memcpy_out(void *data, size_t sz) {
  check(sz, "memcpy_out");

  auto left_to_the_right = _size - filled_start_;
  if (sz > left_to_the_right) {
    std::memcpy(data, &_data.at(filled_start_), left_to_the_right);
    std::memcpy(static_cast<char *>(data) + left_to_the_right, _data.data(),
                sz - left_to_the_right);
  } else {
    std::memcpy(data, &_data.at(filled_start_), sz);
  }
  commit(sz);
}

RingBuffer::const_buffers_type RingBuffer::data() const {
  if (filled_size_ == 0)
    return {};
  auto left_to_the_right = _size - filled_start_;
  if (filled_size_ > left_to_the_right) {
    return {asio::const_buffer(&_data.at(filled_start_), left_to_the_right),
            asio::const_buffer(_data.data(), filled_size_ - left_to_the_right)};
  }
  return const_buffers_type(
      asio::const_buffer(&_data.at(filled_start_), filled_size_));
}

RingBuffer::const_buffers_type RingBuffer::data(std::size_t max_size) const {
  if (filled_size_ == 0)
    return {};
  auto buf_size = std::min(filled_size_, max_size);
  auto left_to_the_right = _size - filled_start_;
  if (buf_size > left_to_the_right) {
    return {asio::const_buffer(&_data.at(filled_start_), left_to_the_right),
            asio::const_buffer(_data.data(), buf_size - left_to_the_right)};
  }
  return const_buffers_type(
      asio::const_buffer(&_data.at(filled_start_), buf_size));
}

RingBuffer::mutable_buffers_type RingBuffer::prepared() {
  if (non_filled_size_ == 0) {
    return {};
  }
  auto left_to_the_right = _size - non_filled_start_;
  if (non_filled_size_ > left_to_the_right) {
    // we have 2 parts
    return {
        asio::mutable_buffer(&_data.at(non_filled_start_), left_to_the_right),
        asio::mutable_buffer(_data.data(),
                             non_filled_size_ - left_to_the_right)};
  }
  return mutable_buffers_type(
      asio::mutable_buffer(&_data.at(non_filled_start_), non_filled_size_));
}

RingBuffer::mutable_buffers_type RingBuffer::prepared(std::size_t max_size) {
  if (non_filled_size_ == 0) {
    return {};
  }
  auto buf_size = std::min(non_filled_size_, max_size);
  auto left_to_the_right = _size - non_filled_start_;
  if (buf_size > left_to_the_right) {
    return {
        asio::mutable_buffer(&_data.at(non_filled_start_), left_to_the_right),
        asio::mutable_buffer(_data.data(), buf_size - left_to_the_right)};
  }
  return mutable_buffers_type(
      asio::mutable_buffer(&_data.at(non_filled_start_), buf_size));
}

bool RingBuffer::empty() const { return filled_size_ == 0; }

std::size_t RingBuffer::ready_size() const { return filled_size_; }

std::size_t RingBuffer::ready_write_size() const { return non_filled_size_; }

bool RingBuffer::below_high_watermark() const {
  return ready_size() < _high_watermark;
}

bool RingBuffer::below_low_watermark() const {
  return ready_size() < _low_watermark;
}

void RingBuffer::check(int len, std::string_view method) const {
  if (len > filled_size_) {
    LOG(ERROR) << "RingBuffer " << method << ": cant read " << len
               << " >= " << filled_size_ << " debug " << this;
    std::terminate();
  }
}

inline char RingBuffer::char_at(std::size_t pos) const {
  if (pos < _size)
    return _data.at(pos);
  return _data.at(pos - _size);
}

using raw_int = union {
  std::array<char, 4> c;
  int i;
};

int RingBuffer::peek_int() const {
  check(4, "peek_int");
  int ret = 0;
  if (filled_size_ + 4 < _size) {
    std::memcpy(&ret, &_data.at(filled_start_), sizeof(ret));
  } else {
    raw_int ri;
    ri.c[0] = char_at(filled_start_);
    ri.c[1] = char_at(filled_start_ + 1);
    ri.c[2] = char_at(filled_start_ + 2);
    ri.c[3] = char_at(filled_start_ + 3);
    ret = ri.i;
  }
  LOG(INFO) << "peek_int: " << ret;
  return ret;
}

buffers_2<std::string_view> RingBuffer::peek_string_view(int len) const {
  check(len, "peek_string_view");
  auto left_to_the_right = _size - filled_start_;
  if (len > left_to_the_right) {
    return {std::string_view(&_data.at(filled_start_), left_to_the_right),
            std::string_view(_data.data(), len - left_to_the_right)};
  } else {
    return buffers_2(std::string_view(&_data.at(filled_start_), len));
  }
}

buffers_2<std::span<const char>> RingBuffer::peek_span(int len) const {
  check(len, "peek_span");
  auto left_to_the_right = _size - filled_start_;
  if (len > left_to_the_right) {
    return {std::span(&_data.at(filled_start_), left_to_the_right),
            std::span(_data.data(), len - left_to_the_right)};
  } else {
    return buffers_2(std::span(&_data.at(filled_start_), len));
  }
}

std::span<char> RingBuffer::peek_linear_span(int len) {
  check(len, "peek_linear_span");
  static_assert(std::same_as<LinnearArray, decltype(_data)>,
                "_data should be linear array, to support liear view");
  return {&_data.at(filled_start_), static_cast<std::size_t>(len)};
}

std::size_t RingBuffer::peek_pos() const { return filled_start_; }

Channel::Channel() {
  buffer_.on_commit_ = [this](){
    if (!buffer_.below_low_watermark()) {
      return;
    };
    decltype(callbacks_on_not_full_) tmp{};
    std::swap(tmp, callbacks_on_not_full_);
    for(auto &&callback: tmp) {
      if (callback.wants_size_ <= buffer_.ready_write_size()) {
        callback.callback_();
      } else {
        callbacks_on_not_full_.emplace_back(callback);
      }
    }
  };
}

RingBuffer& Channel::buffer() noexcept {
  return buffer_;
}

void Channel::add_callback_on_buffer_not_full(Channel::OnBufferNotFull &&callback) noexcept {
  callbacks_on_not_full_.emplace_back(std::move(callback));
}

void Envelope::log() {
  LOG(INFO) << "envelope message type " << message_type << " message size "
            << message_size;
}

bool Decoder::try_read(RingBuffer &state) {
  if (_state == DecoderState::before_envelope) {
    if (state.ready_size() >= sizeof(Envelope)) {
      // we can parse the envelope now
      _envelope.message_type = state.peek_int();
      state.commit(4);
      _envelope.message_size = state.peek_int();
      state.commit(4);
      _state = DecoderState::have_envelope;
      LOG(INFO) << "Handle::try_read state " << (int)_state
                << " envelope, message_size " << _envelope.message_size
                << " message_type " << _envelope.message_type;
      return try_read(state);
    } else {
      return false;
    }
  } else if (_state == DecoderState::have_envelope) {
    return true;
  } else {
    std::terminate();
  }
}

void Decoder::reset() {
  _state = DecoderState::before_envelope;
  _envelope = {};
}

void Encoder::fill_envelope(Envelope envelope, RingBuffer &buff) {
  buff.memcpy_in(&envelope, sizeof(envelope));
}


static std::string make_daytime_string() {
  using namespace std; // For time_t, time and ctime;
  time_t now = time(nullptr);
  return ctime(&now);
}

infinite_timer::infinite_timer(asio::io_context &io_context)
    : timer_(io_context, interval) {
  start();
}

void infinite_timer::start() {
  timer_.expires_at(timer_.expires_at() + interval);
  timer_.async_wait([this](const asio::error_code &error) {
    LOG(INFO) << "timer! " << make_daytime_string();
    this->start();
  });
}

} // namespace am
