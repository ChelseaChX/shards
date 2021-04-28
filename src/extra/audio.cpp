/* SPDX-License-Identifier: BSD 3-Clause "New" or "Revised" License */
/* Copyright © 2021 Giovanni Petrantoni */

#include "blocks/shared.hpp"
#include "runtime.hpp"

// #define BUILD_PARETO_WITH_PMR
#include <pareto/front.h>

#define STB_VORBIS_HEADER_ONLY
#include "extras/stb_vorbis.c" // Enables Vorbis decoding.

#ifdef __APPLE__
#define MA_NO_RUNTIME_LINKING
#endif

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

// The stb_vorbis implementation must come after the implementation of
// miniaudio.
#undef STB_VORBIS_HEADER_ONLY
#include "extras/stb_vorbis.c"

namespace chainblocks {
namespace Audio {

/*

Inner audio chains should not be allowed to have (Pause) or clipping would
happen Also they should probably run like RunChain Detached so that multiple
references to the same chain would be possible and they would just produce
another iteration

*/

struct ChannelData {
  float *outputBuffer;
  std::vector<uint32_t> inChannels;
  std::vector<uint32_t> outChannels;
  BlocksVar blocks;
  ParamVar volume{Var(0.7)};
};

struct Device {
  static constexpr uint32_t DeviceCC = 'sndd';

  static inline Type ObjType{
      {CBType::Object, {.object = {.vendorId = CoreCC, .typeId = DeviceCC}}}};

  // TODO add blocks used as insert for the final mix

  static CBTypesInfo inputTypes() { return CoreInfo::AnyType; }
  static CBTypesInfo outputTypes() { return CoreInfo::AnyType; }

  ma_device _device;
  bool _open{false};
  bool _started{false};

  // (bus, channels hash)
  mutable pareto::spatial_map<uint64_t, 2, std::vector<ChannelData *>> channels;
  mutable pareto::spatial_map<uint64_t, 2, std::vector<float>> outputBuffers;
  ma_uint32 bufferSize{1024};
  ma_uint32 sampleRate{44100};
  std::vector<float> inputScratch;
  uint64_t inputHash;

  static void pcmCallback(ma_device *pDevice, void *pOutput, const void *pInput,
                          ma_uint32 frameCount) {
    auto device = reinterpret_cast<Device *>(pDevice->pUserData);

    // clear all output buffers as from now we will += to them
    for (auto &[_, buffer] : device->outputBuffers) {
      memset(buffer.data(), 0x0, buffer.size() * sizeof(float));
    }

    // depth-first search O(1)
    // ensures lowest latency from ADC to DAC
    for (auto &[nbus, channels] : device->channels) {
      if (channels.size() == 0)
        continue;

      // build the buffer with whatever we need as input
      const auto nchannels = channels[0]->inChannels.size();

      // TODO, review, this one occasionally allocates mem
      device->inputScratch.resize(device->bufferSize * nchannels);

      if (nbus[0] == 0) {
        if (nbus[1] == device->inputHash) {
          // this is the full device input, just copy it
          memcpy(device->inputScratch.data(), pInput,
                 sizeof(float) * nchannels * frameCount);
        } else {
          auto *finput = reinterpret_cast<const float *>(pInput);
          // need to properly compose the input
          for (uint32_t c = 0; c < nchannels; c++) {
            for (ma_uint32 i = 0; i < frameCount; i++) {
              device->inputScratch[(i * nchannels) + c] =
                  finput[(i * nchannels) + channels[0]->inChannels[c]];
            }
          }
        }
      } else {
        // TODO, review, this one occasionally allocates mem
        const auto inputBuffer = device->outputBuffers[nbus];
        if (inputBuffer.size() != 0) {
          std::copy(inputBuffer.begin(), inputBuffer.end(),
                    device->inputScratch.begin());
        } else {
          memset(device->inputScratch.data(), 0x0,
                 device->inputScratch.size() * sizeof(float));
        }
      }

      CBAudio inputPacket{uint32_t(device->sampleRate), //
                          uint16_t(device->bufferSize), //
                          uint16_t(nchannels),          //
                          device->inputScratch.data()};
      Var inputVar(inputPacket);

      // run activations of all channels that need such input
      for (auto channel : channels) {
        // channel->blocks.activate()
      }
    }
  }

  void warmup(CBContext *context) {
    ma_device_config deviceConfig{};
    deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format = ma_format_f32;
    deviceConfig.playback.channels = 2;
    deviceConfig.capture.format = ma_format_f32;
    deviceConfig.capture.channels = 2;
    deviceConfig.sampleRate = sampleRate;
    deviceConfig.periodSizeInFrames = bufferSize;
    deviceConfig.periods = 1;
    deviceConfig.performanceProfile = ma_performance_profile_low_latency;
    deviceConfig.dataCallback = pcmCallback;
    deviceConfig.pUserData = this;

    if (ma_device_init(NULL, &deviceConfig, &_device) != MA_SUCCESS) {
      throw WarmupError("Failed to open default audio device");
    }

    uint64_t inChannels = uint64_t(deviceConfig.capture.channels);
    XXH3_state_s hashState;
    XXH3_INITSTATE(&hashState);
    XXH3_64bits_reset_withSecret(&hashState, CUSTOM_XXH3_kSecret,
                                 XXH_SECRET_DEFAULT_SIZE);
    XXH3_64bits_update(&hashState, &inChannels, sizeof(uint64_t));
    for (CBInt i = 0; i < CBInt(inChannels); i++) {
      XXH3_64bits_update(&hashState, &i, sizeof(CBInt));
    }

    inputHash = XXH3_64bits_digest(&hashState);

    _open = true;
  }

  void cleanup() {
    if (_open) {
      ma_device_uninit(&_device);
    }

    _open = false;
    _started = false;
    channels.clear();
  }

  CBVar activate(CBContext *context, const CBVar &input) {
    if (!_started) {
      if (ma_device_start(&_device) != MA_SUCCESS) {
        throw ActivationError("Failed to start audio device");
      }
      _started = true;
    }
    return input;
  }
};

struct Channel {
  ChannelData _data{};
  CBVar *_device{nullptr};
  uint64_t _inBusNumber;
  OwnedVar _inChannels;
  uint64_t _outBusNumber;
  OwnedVar _outChannels;

  static inline Parameters Params{
      {"Volume",
       CBCCSTR("The volume of this channel."),
       {CoreInfo::FloatType, CoreInfo::FloatVarType}},
      {"Blocks",
       CBCCSTR("The blocks that will process audio data."),
       {CoreInfo::BlocksOrNone}}};

  CBParametersInfo parameters() { return Params; }

  void setParam(int index, const CBVar &value) {
    switch (index) {
    case 0:
      _data.volume = value;
      break;
    case 1:
      _data.blocks = value;
      break;
    default:
      throw InvalidParameterIndex();
    }
  }

  CBVar getParam(int index) {
    switch (index) {
    case 0:
      return _data.volume;
    case 1:
      return _data.blocks;
    default:
      throw InvalidParameterIndex();
    }
  }

  void warmup(CBContext *context) {
    _device = referenceVariable(context, "Audio.Device");
    const auto *d = reinterpret_cast<Device *>(_device->payload.objectValue);
    {
      XXH3_state_s hashState;
      XXH3_INITSTATE(&hashState);
      XXH3_64bits_reset_withSecret(&hashState, CUSTOM_XXH3_kSecret,
                                   XXH_SECRET_DEFAULT_SIZE);
      XXH3_64bits_update(&hashState, &_inBusNumber, sizeof(uint64_t));
      if (_inChannels.valueType == CBType::Seq) {
        for (auto &channel : _inChannels) {
          XXH3_64bits_update(&hashState, &channel.payload.intValue,
                             sizeof(CBInt));
          _data.inChannels.emplace_back(channel.payload.intValue);
        }
      }
      const auto channelsHash = XXH3_64bits_digest(&hashState);
      d->channels(_inBusNumber, channelsHash).emplace_back(&_data);
    }
    {
      XXH3_state_s hashState;
      XXH3_INITSTATE(&hashState);
      XXH3_64bits_reset_withSecret(&hashState, CUSTOM_XXH3_kSecret,
                                   XXH_SECRET_DEFAULT_SIZE);
      XXH3_64bits_update(&hashState, &_outBusNumber, sizeof(uint64_t));
      uint32_t nchannels = 0;
      if (_outChannels.valueType == CBType::Seq) {
        nchannels = _outChannels.payload.seqValue.len;
        for (auto &channel : _outChannels) {
          XXH3_64bits_update(&hashState, &channel.payload.intValue,
                             sizeof(CBInt));
          _data.outChannels.emplace_back(channel.payload.intValue);
        }
      }
      const auto channelsHash = XXH3_64bits_digest(&hashState);
      d->outputBuffers(_outBusNumber, channelsHash)
          .resize(d->bufferSize * nchannels);
      _data.outputBuffer = d->outputBuffers(_outBusNumber, channelsHash).data();
    }

    _data.blocks.warmup(context);
    _data.volume.warmup(context);
  }

  void cleanup() {
    if (_device) {
      releaseVariable(_device);
      _device = nullptr;
    }

    _data.blocks.cleanup();
    _data.volume.cleanup();
  }

  CBVar activate(CBContext *context, const CBVar &input) { return input; }
  // Must be able to handle device inputs, being an instrument, Aux, busses
  // re-route and send
};

struct SineWave {};

struct ReadFile {
  ma_decoder _decoder;
  bool _initialized{false};

  ma_uint32 _channels{2};
  ma_uint64 _nsamples{1024};
  ma_uint32 _sampleRate{44100};
  ma_uint64 _progress{0};
  // what to do when not looped ends? throw?
  bool _looped{false};
  ParamVar _fromSample;
  ParamVar _toSample;
  ParamVar _filename;

  std::vector<float> _buffer;
  bool _done{false};

  static CBTypesInfo inputTypes() { return CoreInfo::NoneType; }
  static CBTypesInfo outputTypes() { return CoreInfo::AudioType; }

  static inline Parameters params{
      {"File",
       CBCCSTR("The audio file to read from (wav,ogg,mp3,flac)."),
       {CoreInfo::StringType, CoreInfo::StringVarType}},
      {"Channels",
       CBCCSTR("The number of desired output audio channels."),
       {CoreInfo::IntType}},
      {"SampleRate",
       CBCCSTR("The desired output sampling rate."),
       {CoreInfo::IntType}},
      {"Samples",
       CBCCSTR("The desired number of samples in the output."),
       {CoreInfo::IntType}},
      {"Looped",
       CBCCSTR("If the file should be played in loop or should stop the chain "
               "when it ends."),
       {CoreInfo::BoolType}},
      {"From",
       CBCCSTR("The starting time in seconds."),
       {CoreInfo::FloatType, CoreInfo::FloatVarType, CoreInfo::NoneType}},
      {"To",
       CBCCSTR("The end time in seconds."),
       {CoreInfo::FloatType, CoreInfo::FloatVarType, CoreInfo::NoneType}}};

  static CBParametersInfo parameters() { return params; }

  void setParam(int index, const CBVar &value) {
    switch (index) {
    case 0:
      _filename = value;
      break;
    case 1:
      _channels = ma_uint32(value.payload.intValue);
      break;
    case 2:
      _sampleRate = ma_uint32(value.payload.intValue);
      break;
    case 3:
      _nsamples = ma_uint64(value.payload.intValue);
      break;
    case 4:
      _looped = value.payload.boolValue;
      break;
    case 5:
      _fromSample = value;
      break;
    case 6:
      _toSample = value;
      break;
    default:
      throw InvalidParameterIndex();
    }
  }

  CBVar getParam(int index) {
    switch (index) {
    case 0:
      return _filename;
    case 1:
      return Var(_channels);
    case 2:
      return Var(_sampleRate);
    case 3:
      return Var(int64_t(_nsamples));
    case 4:
      return Var(_looped);
    case 5:
      return _fromSample;
    case 6:
      return _toSample;
    default:
      throw InvalidParameterIndex();
    }
  }

  void initFile(const std::string_view &filename) {
    ma_decoder_config config =
        ma_decoder_config_init(ma_format_f32, _channels, _sampleRate);
    ma_result res = ma_decoder_init_file(filename.data(), &config, &_decoder);
    if (res != MA_SUCCESS) {
      CBLOG_ERROR("Failed to open audio file {}", filename);
      throw ActivationError("Failed to open audio file");
    }
  }

  void deinitFile() { ma_decoder_uninit(&_decoder); }

  void warmup(CBContext *context) {
    _fromSample.warmup(context);
    _toSample.warmup(context);
    _filename.warmup(context);

    if (!_filename.isVariable() && _filename->valueType == CBType::String) {
      const auto fname = CBSTRVIEW(_filename.get());
      initFile(fname);
      _buffer.resize(size_t(_channels) * size_t(_nsamples));
      _initialized = true;
    }

    _done = false;
    _progress = 0;
  }

  void cleanup() {
    _fromSample.cleanup();
    _toSample.cleanup();
    _filename.cleanup();

    if (_initialized) {
      deinitFile();
      _initialized = false;
    }
  }

  CBVar activate(CBContext *context, const CBVar &input) {
    if (unlikely(_done)) {
      if (_looped) {
        ma_result res = ma_decoder_seek_to_pcm_frame(&_decoder, 0);
        if (res != MA_SUCCESS) {
          throw ActivationError("Failed to seek");
        }
        _done = false;
        _progress = 0;
      } else {
        CB_STOP();
      }
    }

    const auto from = _fromSample.get();
    if (unlikely(from.valueType == CBType::Float && _progress == 0)) {
      const auto sfrom =
          ma_uint64(double(_sampleRate) * from.payload.floatValue);
      ma_result res = ma_decoder_seek_to_pcm_frame(&_decoder, sfrom);
      _progress = sfrom;
      if (res != MA_SUCCESS) {
        throw ActivationError("Failed to seek");
      }
    }

    auto reading = _nsamples;
    const auto to = _toSample.get();
    if (unlikely(to.valueType == CBType::Float)) {
      const auto sto = ma_uint64(double(_sampleRate) * to.payload.floatValue);
      const auto until = _progress + reading;
      if (sto < until) {
        reading = reading - (until - sto);
      }
    }

    // read pcm data every iteration
    ma_uint64 framesRead =
        ma_decoder_read_pcm_frames(&_decoder, _buffer.data(), reading);
    _progress += framesRead;
    if (framesRead < _nsamples) {
      // Reached the end.
      _done = true;
      // zero anything that was not used
      const auto remains = _nsamples - framesRead;
      memset(_buffer.data() + framesRead, 0x0, sizeof(float) * remains);
    }

    return Var(CBAudio{_sampleRate, uint16_t(_nsamples), uint16_t(_channels),
                       _buffer.data()});
  }
};

struct WriteFile {
  ma_encoder _encoder;
  bool _initialized{false};

  ma_uint32 _channels{2};
  ma_uint32 _sampleRate{44100};
  ma_uint64 _progress{0};
  ParamVar _filename;

  static CBTypesInfo inputTypes() { return CoreInfo::AudioType; }
  static CBTypesInfo outputTypes() { return CoreInfo::AudioType; }

  static inline Parameters params{
      {"File",
       CBCCSTR("The audio file to read from (wav,ogg,mp3,flac)."),
       {CoreInfo::StringType, CoreInfo::StringVarType}},
      {"Channels",
       CBCCSTR("The number of desired output audio channels."),
       {CoreInfo::IntType}},
      {"SampleRate",
       CBCCSTR("The desired output sampling rate."),
       {CoreInfo::IntType}}};

  static CBParametersInfo parameters() { return params; }

  void setParam(int index, const CBVar &value) {
    switch (index) {
    case 0:
      _filename = value;
      break;
    case 1:
      _channels = ma_uint32(value.payload.intValue);
      break;
    case 2:
      _sampleRate = ma_uint32(value.payload.intValue);
      break;
    default:
      throw InvalidParameterIndex();
    }
  }

  CBVar getParam(int index) {
    switch (index) {
    case 0:
      return _filename;
    case 1:
      return Var(_channels);
    case 2:
      return Var(_sampleRate);
    default:
      throw InvalidParameterIndex();
    }
  }

  void initFile(const std::string_view &filename) {
    ma_encoder_config config = ma_encoder_config_init(
        ma_resource_format_wav, ma_format_f32, _channels, _sampleRate);
    ma_result res = ma_encoder_init_file(filename.data(), &config, &_encoder);
    if (res != MA_SUCCESS) {
      CBLOG_ERROR("Failed to open audio encoder on file {}", filename);
      throw ActivationError("Failed to open encoder on file");
    }
  }

  void deinitFile() { ma_encoder_uninit(&_encoder); }

  void warmup(CBContext *context) {
    _filename.warmup(context);

    if (!_filename.isVariable() && _filename->valueType == CBType::String) {
      const auto fname = CBSTRVIEW(_filename.get());
      initFile(fname);
      _initialized = true;
    }

    _progress = 0;
  }

  void cleanup() {
    _filename.cleanup();

    if (_initialized) {
      deinitFile();
      _initialized = false;
    }
  }

  CBVar activate(CBContext *context, const CBVar &input) {
    if (input.payload.audioValue.channels != _channels) {
      throw ActivationError("Input has an invalid number of audio channels");
    }
    ma_encoder_write_pcm_frames(&_encoder, input.payload.audioValue.samples,
                                input.payload.audioValue.nsamples);
    return input;
  }
};

void registerBlocks() {
  REGISTER_CBLOCK("Audio.Device", Device);
  REGISTER_CBLOCK("Audio.ReadFile", ReadFile);
  REGISTER_CBLOCK("Audio.WriteFile", WriteFile);
}
} // namespace Audio
} // namespace chainblocks