/*
 * Xenia Edge - Xbox 360 Emulator Libretro Core
 * Libretro Audio Driver
 * Copyright (C) 2024 Xenia Edge Contributors
 *
 * Implements xe::apu::AudioDriver and xe::apu::AudioSystem to capture
 * audio samples from Xenia and forward them to libretro's audio callback.
 */

#ifndef LIBRETRO_AUDIO_DRIVER_H
#define LIBRETRO_AUDIO_DRIVER_H

#include "core_audio_queue.h"
#include <memory>
#include <cstring>

#include "xenia/apu/audio_driver.h"
#include "xenia/apu/audio_system.h"

namespace xe {
namespace apu {
namespace libretro {

class LibretroAudioSystem;

// AudioDriver: one per audio client (and one for the media player), each with
// a ring of its own. The C registry mixes the rings, as a sound
// device mixes its streams - pushing every driver into one shared ring played
// the clients one after another instead of together.
class LibretroAudioDriver : public AudioDriver {
 public:
  // An audio client's frames are 6 channels, big-endian and channel after
  // channel (need_format_conversion); the media player's are interleaved
  // little-endian floats with its own channel count and sample rate, as
  // upstream's SDL driver takes them.
  LibretroAudioDriver(LibretroAudioSystem* system,
                      xe::threading::Semaphore* semaphore, uint32_t frequency,
                      uint32_t channels, bool need_format_conversion);
  ~LibretroAudioDriver() override;

  bool Initialize() override;
  void Shutdown() override;

  bool CanSubmitFrame() const override;
  void SubmitFrame(float* samples) override;
  void Pause() override;
  void Resume() override;
  void SetVolume(float volume) override;

 private:
  void Resample(const float* in, size_t frames);
  XE_NOINLINE void SubmitFrameResampled(float* samples);

  xe_audio_group* group_ = nullptr;
  bool registry_required_ = false;
  xe::threading::Semaphore* semaphore_ = nullptr;
  uint32_t frequency_ = 48000;
  uint32_t channels_ = 6;
  // The output's channels, 2 or 6, as the audio system's when made.
  uint32_t out_channels_ = 2;
  uint32_t channel_samples_ = kChannelSamplesDefault;
  bool need_format_conversion_ = true;
  std::unique_ptr<xe_audio_queue, decltype(&xe_audio_queue_free)> ring_{
      nullptr, &xe_audio_queue_free};
  // Linear resampling to 48 kHz: position between the last two input frames.
  double resample_pos_ = 0.0;
  float last_[6] = {};
};

// AudioSystem that creates LibretroAudioDriver instances.
class LibretroAudioSystem : public AudioSystem {
 public:
  LibretroAudioSystem(cpu::Processor* processor, xe_audio_registry* registry);
  ~LibretroAudioSystem() override;

  static bool IsAvailable() { return true; }

  // Channels the frontend is given, 2 (stereo) or 6 (5.1 as FL FR C LFE SL
  // SR); set before the emulator starts, drivers take it when made.
  static void set_output_channels(uint32_t channels) {
    output_channels_ = channels == 6 ? 6 : 2;
  }
  static uint32_t output_channels() { return output_channels_; }

  std::string name() const override { return "libretro"; }

  AudioDriver* CreateDriver(xe::threading::Semaphore* semaphore,
                             uint32_t frequency, uint32_t channels,
                             bool need_format_conversion) override;
  void DestroyDriver(AudioDriver* driver) override;

  void StopWorker() override;
  void CancelPendingOutput() override;
  xe_audio_group* group() const { return group_.get(); }

 protected:
  X_STATUS CreateDriver(size_t index, xe::threading::Semaphore* semaphore,
                         AudioDriver** out_driver) override;

 private:
  std::unique_ptr<xe_audio_group, decltype(&xe_audio_group_free)> group_{
      nullptr, &xe_audio_group_free};
  // Configured before creating audio workers; immutable during their lifetime.
  static inline uint32_t output_channels_ = 2;
};

}  // namespace libretro
}  // namespace apu
}  // namespace xe

#endif  // LIBRETRO_AUDIO_DRIVER_H
