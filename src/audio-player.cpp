#include "protocol.hpp"
#include <absl/functional/any_invocable.h>
#include <absl/strings/str_cat.h>
#include <absl/utility/utility.h>
#include <asio/io_context.hpp>
#include <cstdio>
#include <functional>
#include <iterator>
#include <memory>

#include <absl/log/log.h>
#include <AudioToolbox/AudioToolbox.h>
#include <mutex>
#include <ostream>
#include <span>
#include <utility>

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#include <minimp3.h>

#include "audio-player.hpp"
#include "protocol.hpp"


namespace am {

OSStatus CallbackRenderProc(void *inRefCon,
              AudioUnitRenderActionFlags *ioActionFlags,
              const AudioTimeStamp *inTimeStamp,
              UInt32 inBusNumber,
              UInt32 inNumberFrames,
              AudioBufferList * ioData);

struct AudioUnitCloser {
  void operator()(void * unit) {
    auto audio_unit = static_cast<AudioUnit>(unit);
    AudioOutputUnitStop(audio_unit);
    AudioUnitUninitialize(audio_unit);
    AudioComponentInstanceDispose(audio_unit);
  } 
};
struct AudioUnitHandle {
  std::unique_ptr<void, AudioUnitCloser> handle;
  AudioUnit operator*() {
    return static_cast<AudioUnit>(handle.get());
  }
};

static void CheckError(OSStatus error, const char *operation)
{
  if (error == noErr) return;
  
  char str[20];
  // see if it appears to be a 4-char-code
  *(UInt32 *)(str + 1) = CFSwapInt32HostToBig(error);
  if (isprint(str[1]) && isprint(str[2]) && isprint(str[3]) && isprint(str[4])) {
    str[0] = str[5] = '\'';
    str[6] = '\0';
  } else
    // no, format it as an integer
    snprintf(str, 20, "%d", (int)error);
  
  fprintf(stderr, "Error: %s (%s)\n", operation, str);
  
  exit(1);
}


static void log_mp3_format(mp3dec_frame_info_t &info) {
  LOG(INFO) << "mp3 format" << std::endl
     << "sample rate       : " << info.hz << std::endl
     << "channels          : " << info.channels << std::endl
     << "bytes per frame   : " << info.frame_bytes << std::endl
     << "bitrate           : " << info.bitrate_kbps << std::endl
     << "frame_offset      : " << info.frame_offset << std::endl
     << "frame_bytes       : " << info.frame_bytes << std::endl;
}

static void log_format(const AudioStreamBasicDescription& format) {
  LOG(INFO) << "default format "
     << "sample rate       : " << format.mSampleRate << std::endl
     << "format ID         : " << format.mFormatID << std::endl
     << "format flags      : " << format.mFormatFlags << std::endl
     << "bytes per packet  : " << format.mBytesPerPacket << std::endl
     << "frames per packet : " << format.mFramesPerPacket << std::endl
     << "bytes per frame   : " << format.mBytesPerFrame << std::endl
     << "channels per frame: " << format.mChannelsPerFrame << std::endl
     << "bits per channel  : " << format.mBitsPerChannel;
}

struct Player {
  using OnLowWatermark = std::function<void()>;

  static std::unique_ptr<Player> create(OnLowWatermark &&on_low_watermark) {
    AudioComponentDescription outputcd = {0}; // 10.6 version
    outputcd.componentType = kAudioUnitType_Output;
    outputcd.componentSubType = kAudioUnitSubType_DefaultOutput;
    outputcd.componentManufacturer = kAudioUnitManufacturer_Apple;
  
    AudioComponent comp = AudioComponentFindNext (nullptr, &outputcd);
    if (comp == nullptr) {
      LOG(ERROR) << "can't get output unit";
      exit(-1);
    }
    AudioUnit output_unit{};
    CheckError(AudioComponentInstanceNew(comp, &output_unit),
          "Couldn't open component for outputUnit");
    
    auto void_unit_handle = std::unique_ptr<void, AudioUnitCloser>(output_unit);
    auto audio_unit_handle = AudioUnitHandle{std::move(void_unit_handle)};
    auto *player = new Player(std::move(audio_unit_handle), std::move(on_low_watermark)); 
    auto res = std::unique_ptr<Player>(player);

    res->setup_unit();
    return res;
  } 
  void callback(AudioBufferList * ioData, const AudioTimeStamp *timestamp, UInt32 inNumberFrames) {
    // LOG(INFO) << "requested number of buffers " << ioData->mNumberBuffers;

    auto channel_size = ioData->mBuffers[0].mDataByteSize;
    // LOG(INFO) << "decoded stream ready to play: " << _output_buffer.ready_size() << " asked for " << inNumberFrames << " committing " << channel_size;
    if (_output_buffer.ready_size() >= channel_size) {
      _output_buffer.memcpy_out(ioData->mBuffers[0].mData, channel_size);
    } else {
      stop();
    }
    
    if (_output_buffer.below_watermark()) {
      _on_low_watermark();
    }

    //_starting_frame_count = j;
  }
void start() {
  _started = true;
  CheckError (AudioOutputUnitStart(*_output_unit), "Couldn't start output unit");
}
void stop() {
  _started = true;
  CheckError (AudioOutputUnitStop(*_output_unit), "Couldn't stop output unit");
}
RingBuffer &buffer() {
  return _output_buffer;
}
bool started() {
  return _started;
}
private:
  Player(AudioUnitHandle output_unit, OnLowWatermark &&on_low_watermark): _output_unit(std::move(output_unit)), _output_buffer(16000000, 20000, 40000), _on_low_watermark(std::move(on_low_watermark)) {
    format_.mSampleRate = 44100;
    format_.mFormatID = kAudioFormatLinearPCM;
    format_.mFormatFlags =  kLinearPCMFormatFlagIsSignedInteger /*| kLinearPCMFormatFlagIsPacked*/;
    format_.mBitsPerChannel = 16;
    format_.mChannelsPerFrame = 2;
    format_.mFramesPerPacket = 1;
    format_.mBytesPerFrame = (format_.mBitsPerChannel * format_.mChannelsPerFrame) / 8;
    format_.mBytesPerPacket = format_.mBytesPerFrame * format_.mFramesPerPacket;
    format_.mReserved = 0;
  }
  void setup_unit() {

    AURenderCallbackStruct input;
    input.inputProc = CallbackRenderProc;
    input.inputProcRefCon = this;
    CheckError(AudioUnitSetProperty(*_output_unit,
        kAudioUnitProperty_SetRenderCallback, 
        kAudioUnitScope_Global,
        0,
        &input,
        sizeof(input)),
      "AudioUnitSetProperty failed: kAudioUnitProperty_SetRenderCallback");

    unsigned int cur_format_size = sizeof(AudioStreamBasicDescription);
    AudioStreamBasicDescription cur_format = {0};
    CheckError(AudioUnitGetProperty(*_output_unit,
                         kAudioUnitProperty_StreamFormat,
                         kAudioUnitScope_Global,
                         0,
                         (void *)&cur_format,
                         &cur_format_size), "AudioUnitGetProperty failed: kAudioUnitProperty_StreamFormat");
    log_format(cur_format);

    CheckError(AudioUnitSetProperty(*_output_unit,
        kAudioUnitProperty_StreamFormat,
        kAudioUnitScope_Input,
        0,
        &format_,
        sizeof(format_)),
      "AudioUnitSetProperty failed: kAudioUnitProperty_StreamFormat");

    cur_format_size = sizeof(AudioStreamBasicDescription);
    cur_format = {};
    CheckError(AudioUnitGetProperty(*_output_unit,
                         kAudioUnitProperty_StreamFormat,
                         kAudioUnitScope_Global,
                         0,
                         (void *)&cur_format,
                         &cur_format_size), "AudioUnitGetProperty failed: kAudioUnitProperty_StreamFormat");
    log_format(cur_format);

    // initialize unit
    CheckError (AudioUnitInitialize(*_output_unit),
        "Couldn't initialize output unit");
  }
  AudioUnitHandle _output_unit;
  double _starting_frame_count {};
  RingBuffer _output_buffer;
  std::atomic_bool _started{false};
  OnLowWatermark _on_low_watermark;
  AudioStreamBasicDescription format_;
};


OSStatus CallbackRenderProc(void *inRefCon,
              AudioUnitRenderActionFlags *ioActionFlags,
              const AudioTimeStamp *inTimeStamp,
              UInt32 inBusNumber,
              UInt32 inNumberFrames,
              AudioBufferList * ioData) {
  
  // LOG(DEBUG) << "CallbackRenderProc flags " << *ioActionFlags << " needs " << inNumberFrames << " frames at " << CFAbsoluteTimeGetCurrent();
  auto *player = static_cast<Player *>(inRefCon);
  player->callback(ioData, inTimeStamp, inNumberFrames);
  
  return noErr;
}



struct Mp3Stream::Pimpl {

  Pimpl(RingBuffer &input, asio::io_context &io_context, asio::io_context::strand &strand, OnLowWatermark &&on_low_watermark): 
  _input(input), _io_context(io_context), _strand(strand), _player(Player::create(
    [this](){
      asio::post(_io_context, [this](){
        decode_next();
      });
    })), _on_low_watermark(std::move(on_low_watermark))/*, _wav_file("./wav.wav", std::ios_base::out | std::ios_base::binary)*/ {
     mp3dec_init(&_mp3d); }

  void decode_next() {
    while ((_input.ready_size() > 0) && (_player->buffer().below_high_watermark()) ) {
      decode_next_inner();
    }
    // _wav_file.flush();
    if (_input.ready_size() == 0) {
      LOG(INFO) << "decode_next: empty input";
    }
    _decoded_frames = 0;
  }

  void decode_next_inner() {
    if (_input.peek_pos() % 200 == 0) {
      log_state();
    }
    mp3dec_frame_info_t info;
    std::memset(&info, 0, sizeof(info));
    std::array<mp3d_sample_t, MINIMP3_MAX_SAMPLES_PER_FRAME> pcm;
    auto input_size = _input.ready_size(); 
    auto input_buf = _input.peek_linear_span(static_cast<int>(input_size));

    int samples = mp3dec_decode_frame(&_mp3d, reinterpret_cast<uint8_t *>(input_buf.data()), input_size, pcm.data(), &info);
    std::call_once(_log_mp3_format_once, [&info](){log_mp3_format(info);});

    if (info.frame_bytes > 0) {
      _input.commit(info.frame_bytes);
      if (samples) {
        _decoded_frames++;
        size_t decoded_size = samples * sizeof(mp3d_sample_t);
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

  void log_state() {
    LOG(INFO) << "input ready " << _input.ready_size() << " output ready " << _player->buffer().ready_size();
  }

  void set_on_low_watermark(OnLowWatermark &&fun) {
    _on_low_watermark = std::move(fun);
  }

  RingBuffer &_input;
  asio::io_context &_io_context;
  asio::io_context::strand &_strand;

  mp3dec_t _mp3d{};
  std::unique_ptr<Player> _player;
  int _decoded_frames {};
  std::once_flag _log_mp3_format_once;
  OnLowWatermark _on_low_watermark;
  //std::ofstream _wav_file;
};

void Mp3Stream::decode_next() {
  _pimpl->decode_next();
}

void Mp3Stream::PimplDeleter::operator()(Pimpl *pimpl) {
  delete pimpl;
}

Mp3Stream::Mp3Stream(RingBuffer &input, asio::io_context &io_context, asio::io_context::strand &strand, OnLowWatermark &&on_low_watermark):_pimpl(new Pimpl(input, io_context, strand, std::move(on_low_watermark))) {}
void Mp3Stream::set_on_low_watermark(OnLowWatermark &&fun) {
  _pimpl->set_on_low_watermark(std::move(fun));
}

} // namespace am
