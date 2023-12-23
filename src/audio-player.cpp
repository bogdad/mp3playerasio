#include "metrics.hpp"
#include "protocol.hpp"
#include <SDL_audio.h>
#include <absl/functional/any_invocable.h>
#include <absl/strings/str_cat.h>
#include <absl/utility/utility.h>
#include <asio/detail/atomic_count.hpp>
#include <asio/io_context.hpp>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>

#include <SDL.h>
#include <absl/log/log.h>
#include <mutex>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <tuple>
#include <utility>

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#define MINIMP3_FLOAT_OUTPUT
#include <minimp3.h>

#include "audio-player.hpp"
#include "protocol.hpp"

namespace am {

static void log_mp3_format(mp3dec_frame_info_t &info) {
  LOG(INFO) << "mp3 format" << std::endl
            << "sample rate       : " << info.hz << std::endl
            << "channels          : " << info.channels << std::endl
            << "bytes per frame   : " << info.frame_bytes << std::endl
            << "bitrate           : " << info.bitrate_kbps << std::endl
            << "frame_offset      : " << info.frame_offset << std::endl
            << "frame_bytes       : " << info.frame_bytes << std::endl;
}

void my_audio_callback(void *userdata, std::uint8_t *stream, int len);

void log_spec(const SDL_AudioSpec &spec) {
  LOG(INFO) << " samples " << spec.samples << " freq " << spec.freq
            << " channels " << (int)spec.channels << " format " << spec.format
            << " userdata " << spec.userdata << " callback " << spec.callback;
}

struct SdlAudio {
  SdlAudio() {
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
      LOG(ERROR) << "cant init sdl";
      exit(-1);
    }
  }
  ~SdlAudio() { SDL_Quit(); }
  SdlAudio(const SdlAudio &) = delete;
  SdlAudio(SdlAudio &&) = delete;
  SdlAudio &operator=(const SdlAudio &) = delete;
  SdlAudio &operator=(SdlAudio &&) = delete;
};

struct SdlAudioDevice {
  SdlAudioDevice(SDL_AudioSpec &&specs)
      : specs_(std::move(specs)) {
    SDL_AudioSpec have_spec;
    audio_dev_id_ = SDL_OpenAudioDevice(NULL, 0, &specs_, &have_spec,
                                        SDL_AUDIO_ALLOW_ANY_CHANGE);

    if (audio_dev_id_ == 0) {
      throw std::runtime_error("sdl2: could not open audio");
    }
    LOG(INFO) << "audo device " << audio_dev_id_ << " want spec ";
    log_spec(specs_);
    LOG(INFO) << "have spec ";
    log_spec(have_spec);
  }
  ~SdlAudioDevice() { SDL_CloseAudioDevice(audio_dev_id_); }
  SdlAudioDevice(const SdlAudioDevice &) = delete;
  SdlAudioDevice(SdlAudioDevice &&) = delete;
  SdlAudioDevice &operator=(const SdlAudioDevice &) = delete;
  SdlAudioDevice &operator=(SdlAudioDevice &&other) = delete;

  void play() { SDL_PauseAudioDevice(audio_dev_id_, 0); }
  void stop() { SDL_PauseAudioDevice(audio_dev_id_, 1); }

  SDL_AudioSpec specs_;
  unsigned int audio_dev_id_{};
};

struct Player {
  using OnLowWatermark = std::function<void()>;

  static std::unique_ptr<Player> create(OnLowWatermark &&on_low_watermark) {
    auto *player = new Player(std::move(on_low_watermark));
    auto res = std::unique_ptr<Player>(player);

    res->setup_unit();
    return res;
  }

  void callback(std::uint8_t *stream, int len) {
    auto start_time = std::chrono::high_resolution_clock::now();
    callbacks_called_.fetch_add(1, std::memory_order_relaxed);
    auto audio_len = static_cast<int>(_output_buffer.buffer().ready_size());
    if (audio_len == 0) {
      stop();
      return;
    }
    std::memset(stream, 0, len);
    if (len > audio_len) {
      metric_underflows_.add(1);
    }
    metric_len_.add(len);
    metric_output_buff_.add(audio_len);

    len = (len > audio_len ? audio_len : len);
    _output_buffer.buffer().memcpy_out(stream, len);

    if (_output_buffer.buffer().below_low_watermark()) {
      on_low_watermark_();
    }
    auto end_time = std::chrono::high_resolution_clock::now();
    memtric_callback_micros_.add(std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count());
  }
  void start() {
    LOG(INFO) << "audo device started";
    started_ = true;
    if (audio_device_.has_value())
      audio_device_->play();
  }
  void stop() {
    LOG(INFO) << "audio device stopped";
    started_ = false;
    if (audio_device_.has_value())
      audio_device_->stop();
  }
  Channel &buffer() { return _output_buffer; }
  bool started() { return started_; }

  int callbacks_called() {
    return callbacks_called_.load(std::memory_order_relaxed);
  }
  void reset_stat() {
    metric_len_.reset();
    metric_underflows_.reset();
    metric_output_buff_.reset();
    memtric_callback_micros_.reset();
  }

  void log_stat() {
    LOG(INFO) << metric_underflows_;
    LOG(INFO) << metric_len_;
    LOG(INFO) << metric_output_buff_;
    LOG(INFO) << memtric_callback_micros_;
  }
private:
  Player(OnLowWatermark &&on_low_watermark)
      : on_low_watermark_(std::move(on_low_watermark)) {}

  void setup_unit() {
    SDL_AudioSpec spec;
    SDL_zero(spec);
    spec.freq = 44100;
    spec.format = AUDIO_F32;
    spec.channels = 2;
    spec.samples = 1024;
    spec.callback = my_audio_callback;
    spec.userdata = this;
    audio_device_.emplace(std::move(spec));
  }

  SdlAudio audio_{};
  std::optional<SdlAudioDevice> audio_device_{};
  double _starting_frame_count{};
  Channel _output_buffer{};
  std::atomic_bool started_{false};
  OnLowWatermark on_low_watermark_;
  std::atomic_int callbacks_called_{};
  Metric<int> metric_underflows_ = Metric<int>::create_counter("underflows");
  Metric<int> metric_len_ = Metric<int>::create_average("sdl callback stream len");
  Metric<int> metric_output_buff_ = Metric<int>::create_average("output buff");
  Metric<long> memtric_callback_micros_ = Metric<long>::create_average("callback_micros");
};

void my_audio_callback(void *userdata, std::uint8_t *stream, int len) {
  auto *player = static_cast<Player *>(userdata);
  player->callback(stream, len);
}

struct Mp3Stream::Pimpl {
  Pimpl(Channel &input, asio::io_context &io_context,
        asio::io_context::strand &strand)
      : input_(input)
      , io_context_(io_context)
      , strand_(strand)
      , player_(Player::create([this]() {
        asio::post(io_context_, [this]() { decode_next(); });
      })) {
    mp3dec_init(&mp3d_);
  }

  void decode_next() {
    if (waiting_for_play_) {
      return;
    }
    while ((input_.buffer().ready_size() > 0) && player_->buffer().buffer().below_high_watermark()) {
      // TODO: would we loose data if output buffer is not below high watermark and input is empty? ie noone will call us again 
      // no, because in below-low-watermark we will restart decode next.
      decode_next_inner();
      if (waiting_for_play_) break;
    }
    if (input_.buffer().ready_size() == 0) {
      return;
    }
    decoded_frames_ = 0;
    
    auto callbacks_called = player_->callbacks_called();
    if (callbacks_called - last_callbacks_called_ > 399) {
      player_->log_stat();
      last_callbacks_called_ = callbacks_called;
      player_->reset_stat();
    }
  }

  void decode_next_inner() {
    if (input_.buffer().peek_pos() % 200 == 0) {
      // log_state();
    }
    mp3dec_frame_info_t info;
    std::memset(&info, 0, sizeof(info));
    std::array<mp3d_sample_t, MINIMP3_MAX_SAMPLES_PER_FRAME> pcm;
    auto &buffer = input_.buffer();
    auto input_size = buffer.ready_size();
    auto input_buf = buffer.peek_linear_span(static_cast<int>(input_size));

    int samples = mp3dec_decode_frame(
        &mp3d_, reinterpret_cast<uint8_t *>(input_buf.data()), input_size,
        pcm.data(), &info);
    std::call_once(log_mp3_format_once_, [&info]() { log_mp3_format(info); });
    auto &player_buffer = player_->buffer().buffer();
    if (info.frame_bytes > 0) {
      size_t decoded_size = 2 * samples * sizeof(mp3d_sample_t);
      if (decoded_size > player_buffer.ready_write_size()) {
        LOG(INFO) << "decode_next: want to put " << decoded_size << " can put " << player_buffer.ready_write_size();
        waiting_for_play_ = true;
        player_->buffer().add_callback_on_buffer_not_full(OnBufferNotFullSz {
        // TODO: check lifetime of this
        strand_.wrap([this](){
          waiting_for_play_ = false;
          decode_next();
        }), 
        decoded_size
      });
      } else {
        buffer.commit(info.frame_bytes);
        if (samples) {
          decoded_frames_++;
          // TODO: what if it does not fit
          player_buffer.memcpy_in(pcm.data(), decoded_size);
        }
      }
    }

    if (samples && !player_->buffer().buffer().below_low_watermark()) {
      if (!player_->started()) {
        LOG(INFO) << "starting player";
        player_->start();
      }
    }
  }

  Channel &buffer() { return input_; }

  void log_state() {
    auto &buffer = input_.buffer();
    LOG(INFO) << "input ready " << buffer.ready_size() << " output ready "
              << player_->buffer().buffer().ready_size();
  }

  Channel &input_;
  asio::io_context &io_context_;
  asio::io_context::strand &strand_;

  mp3dec_t mp3d_{};
  std::unique_ptr<Player> player_;
  int decoded_frames_{};
  std::once_flag log_mp3_format_once_;
  int last_callbacks_called_ {-100};
  bool waiting_for_play_ {false};
};

void Mp3Stream::decode_next() { pimpl_->decode_next(); }

Mp3Stream::Mp3Stream(Channel &input, asio::io_context &io_context,
                     asio::io_context::strand &strand)
    : pimpl_(new Pimpl(input, io_context, strand)){};

Channel &Mp3Stream::buffer() { return pimpl_->buffer(); }

void Mp3Stream::PimplDeleter::operator()(Pimpl *pimpl) { delete pimpl; }
} // namespace am
