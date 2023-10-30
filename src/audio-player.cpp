#include "protocol.hpp"
#include <absl/functional/any_invocable.h>
#include <absl/strings/str_cat.h>
#include <absl/utility/utility.h>
#include <asio/io_context.hpp>
#include <cstdio>
#include <memory>

#include <absl/log/log.h>
#include <AudioToolbox/AudioToolbox.h>
#include <mutex>
#include <ostream>
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
    LOG(INFO) << "Player: callback";
    const int sine_frequency = 880.0;
    double j = _starting_frame_count;
    //  double cycleLength = 44100. / 2200./*frequency*/;
    double cycle_length = 44100. / sine_frequency;
    
    /*int frame = 0;
    for (frame = 0; frame < inNumberFrames; ++frame) 
    {
      float *data = (float*)ioData->mBuffers[0].mData;
      (data)[frame] = (Float32)sin (2 * M_PI * (j / cycle_length));
      
      // copy to right channel too
      data = (float *)ioData->mBuffers[1].mData;
      (data)[frame] = (Float32)sin (2 * M_PI * (j / cycle_length));
      
      j += 1.0;
      if (j > cycle_length)
        j -= cycle_length;
    }*/

    auto channel_size = 2*inNumberFrames;
    LOG(INFO) << "decoded stream ready to play: " << _output_buffer.ready_size() << " asked for " << inNumberFrames << " committing " << channel_size;
    if (_output_buffer.ready_size() >= channel_size) {
      _output_buffer.memcpy_out(ioData->mBuffers[0].mData, channel_size);
      ioData->mBuffers[0].mDataByteSize = channel_size/2;
      //ioData->mBuffers[1].mDataByteSize = channel_size/2;
    } else {
      stop();
    }
    
    if (_output_buffer.below_watermark()) {
      _on_low_watermark();
    }

    _starting_frame_count = j;
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
  Player(AudioUnitHandle output_unit, OnLowWatermark &&on_low_watermark): _output_unit(std::move(output_unit)), _output_buffer(16000000, 20000), _on_low_watermark(std::move(on_low_watermark)) {
    format_.mSampleRate = 44100;
    format_.mFormatID = kAudioFormatLinearPCM;
    format_.mFormatFlags = kLinearPCMFormatFlagIsPacked |
                         kLinearPCMFormatFlagIsSignedInteger;
    format_.mBitsPerChannel = 16;
    format_.mChannelsPerFrame = 2;
    format_.mFramesPerPacket = 1;
    format_.mBytesPerPacket = (format_.mBitsPerChannel * format_.mChannelsPerFrame) / 8;
    format_.mBytesPerFrame = format_.mBytesPerPacket;
    format_.mReserved = 0;
  }
  void setup_unit() {

    // initialize unit
    CheckError (AudioUnitInitialize(*_output_unit),
        "Couldn't initialize output unit");


    AURenderCallbackStruct input;
    input.inputProc = CallbackRenderProc;
    input.inputProcRefCon = this;
    CheckError(AudioUnitSetProperty(*_output_unit,
        kAudioUnitProperty_SetRenderCallback, 
        kAudioUnitScope_Input,
        0,
        &input,
        sizeof(input)),
      "AudioUnitSetProperty failed: kAudioUnitProperty_SetRenderCallback");

    unsigned int cur_format_size = sizeof(AudioStreamBasicDescription);
    AudioStreamBasicDescription cur_format = {0};
    CheckError(AudioUnitGetProperty(*_output_unit,
                         kAudioUnitProperty_StreamFormat,
                         kAudioUnitScope_Output,
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
                         kAudioUnitScope_Output,
                         0,
                         (void *)&cur_format,
                         &cur_format_size), "AudioUnitGetProperty failed: kAudioUnitProperty_StreamFormat");
    log_format(cur_format);



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
  
  LOG(INFO) << "SineWaveRenderProc flags " << *ioActionFlags << " needs " << inNumberFrames << " frames at " << CFAbsoluteTimeGetCurrent();
  auto *player = static_cast<Player *>(inRefCon);
  player->callback(ioData, inTimeStamp, inNumberFrames);
  
  return noErr;
}



struct Mp3Stream::Pimpl {

  Pimpl(RingBuffer &input, asio::io_context &io_context, asio::io_context::strand &strand): 
  _input(input), _io_context(io_context), _strand(strand), _player(Player::create(
    [this](){
      asio::post(_io_context, [this](){
        decode_next();
      });
    })) { mp3dec_init(&_mp3d); }

  void decode_next() {
    while ((_input.ready_size() > 0) && (_player->buffer().below_watermark()) ) {
      decode_next_inner();
    }
    if (_input.ready_size() == 0) {
      LOG(INFO) << "decode_next: empty input";
    }
    _decoded_frames = 0;
  }

  void decode_next_inner() {
    log_state();
    mp3dec_frame_info_t info;
    std::memset(&info, 0, sizeof(info));
    std::array<mp3d_sample_t, MINIMP3_MAX_SAMPLES_PER_FRAME> pcm;
    auto input_size = _input.ready_size(); 
    auto input_buf = _input.peek_linear_span(static_cast<int>(input_size));

    int samples = mp3dec_decode_frame(&_mp3d, reinterpret_cast<uint8_t *>(input_buf.data()), input_size, pcm.data(), &info);
    std::call_once(log_mp3_format_once_, [&info](){log_mp3_format(info);});

    LOG(INFO) << " input start offset " << _input.peek_pos();
    if (info.frame_bytes) {
      _input.commit(info.frame_bytes);
    }
    if (samples) {
      _decoded_frames++;
      _player->buffer().memcpy_in(pcm.data(), info.frame_bytes);
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

  RingBuffer &_input;
  asio::io_context &_io_context;
  asio::io_context::strand &_strand;

  mp3dec_t _mp3d{};
  std::unique_ptr<Player> _player;
  int _decoded_frames {};
  std::once_flag log_mp3_format_once_;
};

void Mp3Stream::decode_next() {
  _pimpl->decode_next();
}

void Mp3Stream::PimplDeleter::operator()(Pimpl *pimpl) {
  delete pimpl;
}

Mp3Stream::Mp3Stream(RingBuffer &input, asio::io_context &io_context, asio::io_context::strand &strand):_pimpl(new Pimpl(input, io_context, strand)) {}

} // namespace am
