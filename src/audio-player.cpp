#include "protocol.hpp"
#include <SDL_audio.h>
#include <absl/functional/any_invocable.h>
#include <absl/strings/str_cat.h>
#include <absl/utility/utility.h>
#include <asio/detail/atomic_count.hpp>
#include <asio/io_context.hpp>
#include <atomic>
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
    auto audio_len = static_cast<int>(_output_buffer.ready_size());
    // LOG(INFO) << "callback " << (void *) stream << " len " << len <<
    // "ready_size " << audio_len;

    if (audio_len == 0) {
      stop();
      return;
    }
    std::memset(stream, 0, len);
    len = (len > audio_len ? audio_len : len);
    _output_buffer.memcpy_out(stream, len);

    if (_output_buffer.below_watermark()) {
      on_low_watermark_();
    }
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
  RingBuffer &buffer() { return _output_buffer; }
  bool started() { return started_; }

private:
  Player(OnLowWatermark &&on_low_watermark)
      : _output_buffer(16000000, 40000, 80000)
      , on_low_watermark_(std::move(on_low_watermark)) {}

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
  RingBuffer _output_buffer;
  std::atomic_bool started_{false};
  OnLowWatermark on_low_watermark_;
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
    while ((input_.buffer().ready_size() > 0) &&
           (player_->buffer().below_high_watermark())) {
      decode_next_inner();
    }
    decoded_frames_ = 0;
  }

  void decode_next_inner() {
    if (input_.buffer().peek_pos() % 200 == 0) {
      log_state();
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

    if (info.frame_bytes > 0) {
      buffer.commit(info.frame_bytes);
      if (samples) {
        decoded_frames_++;
        size_t decoded_size = 2 * samples * sizeof(mp3d_sample_t);
        // auto span = std::span{pcm.data(), decoded_size};
        // _wav_file << span.data();
        player_->buffer().memcpy_in(pcm.data(), decoded_size);
      }
    }

    if (samples && !player_->buffer().below_watermark()) {
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
              << player_->buffer().ready_size();
  }

  Channel &input_;
  asio::io_context &io_context_;
  asio::io_context::strand &strand_;

  mp3dec_t mp3d_{};
  std::unique_ptr<Player> player_;
  int decoded_frames_{};
  std::once_flag log_mp3_format_once_;
};

void Mp3Stream::decode_next() { pimpl_->decode_next(); }

Mp3Stream::Mp3Stream(Channel &input, asio::io_context &io_context,
                     asio::io_context::strand &strand)
    : pimpl_(new Pimpl(input, io_context, strand)){};

Channel &Mp3Stream::buffer() { return pimpl_->buffer(); }

void Mp3Stream::PimplDeleter::operator()(Pimpl *pimpl) { delete pimpl; }
} // namespace am
