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
#include <span>
#include <stdexcept>
#include <utility>

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#define MINIMP3_FLOAT_OUTPUT
#include <minimp3.h>

#include "audio-player.hpp"
#include "protocol.hpp"

namespace am {

static void log_mp3_format(mp3dec_frame_info_t& info)
{
  LOG(INFO) << "mp3 format" << std::endl
            << "sample rate       : " << info.hz << std::endl
            << "channels          : " << info.channels << std::endl
            << "bytes per frame   : " << info.frame_bytes << std::endl
            << "bitrate           : " << info.bitrate_kbps << std::endl
            << "frame_offset      : " << info.frame_offset << std::endl
            << "frame_bytes       : " << info.frame_bytes << std::endl;
}

void my_audio_callback(void* userdata, std::uint8_t* stream, int len);

void log_spec(SDL_AudioSpec const& spec)
{
  LOG(INFO)
      << " samples " << spec.samples
      << " freq " << spec.freq
      << " channels " << (int)spec.channels
      << " format " << spec.format
      << " userdata " << spec.userdata
      << " callback " << spec.callback;
}

struct SdlAudio {
  SdlAudio()
  {
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
      LOG(ERROR) << "cant init sdl";
      exit(-1);
    }
  }
  ~SdlAudio()
  {
    SDL_Quit();
  }
  SdlAudio(SdlAudio const&) = delete;
  SdlAudio(SdlAudio&&) = delete;
  SdlAudio& operator=(SdlAudio const&) = delete;
  SdlAudio& operator=(SdlAudio&&) = delete;
};

struct SdlAudioDevice {
  SdlAudioDevice(SDL_AudioSpec&& specs)
      : specs_(std::move(specs))
  {
    SDL_AudioSpec have_spec;
    audio_dev_id_ = SDL_OpenAudioDevice(NULL, 0, &specs_,
        &have_spec, SDL_AUDIO_ALLOW_ANY_CHANGE);

    if (audio_dev_id_ == 0) {
      throw std::runtime_error("sdl2: could not open audio");
    }
    LOG(INFO) << "audo device " << audio_dev_id_ << " want spec ";
    log_spec(specs_);
    LOG(INFO) << "have spec ";
    log_spec(have_spec);
  }
  ~SdlAudioDevice()
  {
    SDL_CloseAudioDevice(audio_dev_id_);
  }
  SdlAudioDevice(SdlAudioDevice const&) = delete;
  SdlAudioDevice(SdlAudioDevice&&) = delete;
  SdlAudioDevice& operator=(SdlAudioDevice const&) = delete;
  SdlAudioDevice& operator=(SdlAudioDevice&& other) = delete;

  void play()
  {
    SDL_PauseAudioDevice(audio_dev_id_, 0);
  }
  void stop()
  {
    SDL_PauseAudioDevice(audio_dev_id_, 1);
  }

  SDL_AudioSpec specs_;
  unsigned int audio_dev_id_ {};
};

struct Player {
  using OnLowWatermark = std::function<void()>;

  static std::unique_ptr<Player> create(OnLowWatermark&& on_low_watermark)
  {
    auto* player = new Player(std::move(on_low_watermark));
    auto res = std::unique_ptr<Player>(player);

    res->setup_unit();
    return res;
  }

  void callback(std::uint8_t* stream, int len)
  {
    auto audio_len = static_cast<int>(_output_buffer.ready_size());
    // LOG(INFO) << "callback " << (void *) stream << " len " << len << "ready_size " << audio_len;

    if (audio_len == 0) {
      stop();
      return;
    }
    std::memset(stream, 0, len);
    len = (len > audio_len ? audio_len : len);
    _output_buffer.memcpy_out(stream, len);

    if (_output_buffer.below_watermark()) {
      _on_low_watermark();
    }
  }
  void start()
  {
    LOG(INFO) << "audo device started";
    _started = true;
    if (audio_device_.has_value())
      audio_device_->play();
  }
  void stop()
  {
    LOG(INFO) << "audio device stopped";
    _started = false;
    if (audio_device_.has_value())
      audio_device_->stop();
  }
  RingBuffer& buffer()
  {
    return _output_buffer;
  }
  bool started()
  {
    return _started;
  }

  private:
  Player(OnLowWatermark&& on_low_watermark)
      : _output_buffer(16000000, 40000, 80000)
      , _on_low_watermark(std::move(on_low_watermark))
  {
  }

  void setup_unit()
  {
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

  SdlAudio _audio {};
  std::optional<SdlAudioDevice> audio_device_ {};
  double _starting_frame_count {};
  RingBuffer _output_buffer;
  std::atomic_bool _started { false };
  OnLowWatermark _on_low_watermark;
};

void my_audio_callback(void* userdata, std::uint8_t* stream, int len)
{
  auto* player = static_cast<Player*>(userdata);
  player->callback(stream, len);
}

struct Mp3Stream::Pimpl {

  Pimpl(RingBuffer& input, asio::io_context& io_context, asio::io_context::strand& strand)
      : _input(input)
      , _io_context(io_context)
      , _strand(strand)
      , _player(Player::create(
            [this]() {
              asio::post(_io_context, [this]() {
                decode_next();
              });
            }))
  {
    mp3dec_init(&_mp3d);
  }

  void decode_next()
  {
    while ((_input.ready_size() > 0) && (_player->buffer().below_high_watermark())) {
      decode_next_inner();
    }
    _decoded_frames = 0;
  }

  void decode_next_inner()
  {
    if (_input.peek_pos() % 200 == 0) {
      log_state();
    }
    mp3dec_frame_info_t info;
    std::memset(&info, 0, sizeof(info));
    std::array<mp3d_sample_t, MINIMP3_MAX_SAMPLES_PER_FRAME> pcm;
    auto input_size = _input.ready_size();
    auto input_buf = _input.peek_linear_span(static_cast<int>(input_size));

    int samples = mp3dec_decode_frame(&_mp3d, reinterpret_cast<uint8_t*>(input_buf.data()), input_size, pcm.data(), &info);
    std::call_once(_log_mp3_format_once, [&info]() { log_mp3_format(info); });

    if (info.frame_bytes > 0) {
      _input.commit(info.frame_bytes);
      if (samples) {
        _decoded_frames++;
        size_t decoded_size = 2 * samples * sizeof(mp3d_sample_t);
        // auto span = std::span{pcm.data(), decoded_size};
        // _wav_file << span.data();
        _player->buffer().memcpy_in(pcm.data(), decoded_size);
      }
    }
    if (_input.below_watermark()) {
      _on_low_watermark();
    }

    if (samples && !_player->buffer().below_watermark()) {
      if (!_player->started()) {
        LOG(INFO) << "starting player";
        _player->start();
      }
    }
  }

  RingBuffer& buffer()
  {
    return _input;
  }

  void log_state()
  {
    LOG(INFO) << "input ready " << _input.ready_size() << " output ready " << _player->buffer().ready_size();
  }

  void set_on_low_watermark(OnLowWatermark&& fun)
  {
    _on_low_watermark = std::move(fun);
  }

  RingBuffer& _input;
  asio::io_context& _io_context;
  asio::io_context::strand& _strand;

  mp3dec_t _mp3d {};
  std::unique_ptr<Player> _player;
  int _decoded_frames {};
  std::once_flag _log_mp3_format_once;
  OnLowWatermark _on_low_watermark;
};

void Mp3Stream::decode_next()
{
  _pimpl->decode_next();
}

Mp3Stream::Mp3Stream(RingBuffer& input, asio::io_context& io_context, asio::io_context::strand& strand)
    : _pimpl(new Pimpl(input, io_context, strand)) {};

RingBuffer& Mp3Stream::buffer()
{
  return _pimpl->buffer();
}

void Mp3Stream::PimplDeleter::operator()(Pimpl* pimpl)
{
  delete pimpl;
}
} // namespace am
