#include "protocol.hpp"
#include <absl/strings/str_cat.h>
#include <absl/utility/utility.h>
#include <cstdio>
#include <memory>

#include <absl/log/log.h>
#include <AudioToolbox/AudioToolbox.h>
#include <utility>

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#include <minimp3.h>

#include "audio-player.hpp"
#include "protocol.hpp"


namespace am {

OSStatus SineWaveRenderProc(void *inRefCon,
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

struct Player {
  static std::unique_ptr<Player> create() {
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
    auto *player = new Player(std::move(audio_unit_handle)); 
    auto res = std::unique_ptr<Player>(player);

    res->set_callback();
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

    auto channel_size = 2*sizeof(float)*inNumberFrames;
    LOG(INFO) << "decoded stream ready to play: " << _output_buffer.ready_size() << " asked for " << inNumberFrames << " committing " << channel_size;
    _output_buffer.memcpy_out(ioData->mBuffers[0].mData, channel_size/2);
    _output_buffer.memcpy_out(ioData->mBuffers[1].mData, channel_size/2);

    
    _starting_frame_count = j;
  }
void start() {
  _started = true;
  CheckError (AudioOutputUnitStart(*_output_unit), "Couldn't start output unit");
}
RingBuffer &buffer() {
  return _output_buffer;
}
bool started() {
  return _started;
}
private:
  Player(AudioUnitHandle output_unit): _output_unit(std::move(output_unit)), _output_buffer(16000000) {}
  void set_callback() {
    AURenderCallbackStruct input;
    input.inputProc = SineWaveRenderProc;
    input.inputProcRefCon = this;
    CheckError(AudioUnitSetProperty(*_output_unit,
                    kAudioUnitProperty_SetRenderCallback, 
                    kAudioUnitScope_Input,
                    0,
                    &input, 
                    sizeof(input)),
           "AudioUnitSetProperty failed");

    // initialize unit
    CheckError (AudioUnitInitialize(*_output_unit),
        "Couldn't initialize output unit");
  }
  AudioUnitHandle _output_unit;
  double _starting_frame_count {};
  RingBuffer _output_buffer;
  std::atomic_bool _started{false};
};


OSStatus SineWaveRenderProc(void *inRefCon,
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

  Pimpl(): _player(Player::create()) { mp3dec_init(&_mp3d); }

  void decode_next(RingBuffer &input, asio::io_context &io_context,  asio::io_context::strand &strand) {
    while ((input.ready_size() > 0) && (_decoded_frames < 30)) {
      decode_next_inner(input, io_context,  strand);
    }
    _decoded_frames = 0;
  }

  void decode_next_inner(RingBuffer &input, asio::io_context &io_context,  asio::io_context::strand &strand) {
    mp3dec_frame_info_t info;
    std::memset(&info, 0, sizeof(info));
    std::array<mp3d_sample_t, MINIMP3_MAX_SAMPLES_PER_FRAME> pcm;
    auto input_size = input.ready_size(); 
    auto input_buf = input.peek_linear_span(static_cast<int>(input_size));

    LOG(INFO) << "decode_next: ready size " << input_size;
    int samples = mp3dec_decode_frame(&_mp3d, reinterpret_cast<uint8_t *>(input_buf.data()), input_size, pcm.data(), &info);
    LOG(INFO) << "received samples " << samples << " frame_bytes " << info.frame_bytes << " channels: "<< info.channels;
    if (info.frame_bytes) {
      LOG(INFO) << "committed " << info.frame_bytes;
      input.commit(info.frame_bytes);
      if (input.ready_size() > 0) {
        LOG(INFO) << "enqueue";
        _player->buffer().enqueue_on_commit_func([&input, this, &io_context, &strand](){
          // not cool, need to reschedule to the io thread from audio.
          LOG(INFO) << "reposting";
          strand.post([&input, this, &io_context, &strand](){
            this->decode_next(input, io_context, strand);});
        }
        );
      }
    }
    if (samples) {
      _decoded_frames++;
      _player->buffer().memcpy_in(pcm.data(), info.frame_bytes);
    }
    if (samples && _decoded_frames > 20) {
      if (!_player->started()) {
        LOG(INFO) << "starting player";
        _player->start();
      }
    }
  }

  mp3dec_t _mp3d{};
  std::unique_ptr<Player> _player;
  int _decoded_frames {};
};

void Mp3Stream::decode_next(RingBuffer &input, asio::io_context &io_context,  asio::io_context::strand &strand) {
  _pimpl->decode_next(input, io_context, strand);
}

void Mp3Stream::PimplDeleter::operator()(Pimpl *pimpl) {
  delete pimpl;
}

Mp3Stream::Mp3Stream():_pimpl(new Pimpl) {}

} // namespace am
