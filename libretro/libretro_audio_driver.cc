/*
 * Xenia Edge - Xbox 360 Emulator Libretro Core
 * Libretro Audio Driver Implementation
 * Copyright (C) 2024 Xenia Edge Contributors
 */

#include "libretro_audio_driver.h"

#include "xenia/apu/conversion.h"

namespace xe {
namespace apu {
namespace libretro {

// ---- LibretroAudioDriver --------------------------------------------------

LibretroAudioDriver::LibretroAudioDriver(LibretroAudioSystem* system,
                                         xe::threading::Semaphore* semaphore,
                                         uint32_t frequency, uint32_t channels,
                                         bool need_format_conversion,
                                         retro_atomic_int_t* processed)
    : system_(system), processed_(processed), group_(system ? system->group() : nullptr),
      registry_required_(system != nullptr),
      semaphore_(semaphore),
      frequency_(frequency ? frequency : 48000),
      channels_(channels == 2 ? 2 : 6),
      // Native XAudio submits 256 frames of six channels. The existing media
      // caller separately submits 1536 interleaved floats; this is its legacy
      // chunk contract, not a hardware stereo quantum or retro_run cadence.
      channel_samples_(channels == 2 ? 768 : kChannelSamplesDefault),
      out_channels_(LibretroAudioSystem::output_channels()),
      need_format_conversion_(need_format_conversion) {}

LibretroAudioDriver::~LibretroAudioDriver() {
  Shutdown();
}

bool LibretroAudioDriver::Initialize() {
  if (ring_) return true;
  const size_t maximum_frames = frequency_ == 48000 ? channel_samples_ :
      (size_t(channel_samples_) * 48000 + frequency_ - 1) / frequency_ + 1;
  borrowed_output_ = registry_required_ && frequency_ == 48000;
  if (borrowed_output_)
    ring_.reset(xe_audio_group_queue_new_borrowed(group_, channel_samples_, FrameConsumed, this));
  else
    ring_.reset(registry_required_ ? xe_audio_group_queue_new_packet(group_,maximum_frames,FrameConsumed,this)
                                  : xe_audio_queue_new(out_channels_));
  if (!ring_) {
    ring_.reset();
    return false;
  }
  group_ = nullptr;
  return true;
}

void LibretroAudioDriver::Shutdown() { xe_audio_queue_stop(ring_.get()); }

void LibretroAudioDriver::FrameConsumed(void* context, int consumed) {
  LibretroAudioDriver* driver = static_cast<LibretroAudioDriver*>(context);
  if (consumed) {
    if (driver->processed_) retro_atomic_fetch_add_int(driver->processed_, 1);
  } else {
    if (driver->semaphore_) driver->semaphore_->Release(1, nullptr);
    if (driver->processed_ && driver->system_) driver->system_->NotifyOutputSpace();
  }
}

void LibretroAudioDriver::Resample(const float* in, size_t frames) {
  const size_t ch = out_channels_;
  // Linear interpolation between consecutive input frames; last_ carries the
  // previous frame over from the last call so frames join without a click.
  const double step = double(frequency_) / 48000.0;
  float out[2048 * 6];
  size_t n = 0;
  while (resample_pos_ < double(frames)) {
    const size_t i = size_t(resample_pos_);
    const float t = float(resample_pos_ - double(i));
    const float* a = i == 0 ? last_ : &in[(i - 1) * ch];
    const float* b = &in[i * ch];
    for (size_t c = 0; c < ch; ++c) {
      out[n++] = a[c] + (b[c] - a[c]) * t;
    }
    if (n == sizeof(out) / sizeof(out[0])) {
      xe_audio_queue_write(ring_.get(), out, n);
      n = 0;
    }
    resample_pos_ += step;
  }
  resample_pos_ -= double(frames);
  std::memcpy(last_, &in[(frames - 1) * ch], ch * sizeof(float));
  if (n) xe_audio_queue_write(ring_.get(), out, n);
}

bool LibretroAudioDriver::CanSubmitFrame() const {
  return xe_audio_queue_can_write_frames(ring_.get(), channel_samples_) != 0;
}

void LibretroAudioDriver::SubmitFrame(float* samples) {
  if (borrowed_output_) {
    xe_audio_queue_submit_borrowed(ring_.get(), samples, channel_samples_, channels_,
        channels_ == 6 && need_format_conversion_, xe_audio_queue_get_volume(ring_.get()));
    return;
  }
  if (frequency_ != 48000) {
    SubmitFrameResampled(samples);
    return;
  }
  if (!xe_audio_queue_paused(ring_.get())) {
    xe_audio_queue_write_converted(ring_.get(), samples, channel_samples_,
        channels_, channels_ == 6 && need_format_conversion_, xe_audio_queue_get_volume(ring_.get()));
  }
  xe_audio_queue_finish_packet(ring_.get());
}

void LibretroAudioDriver::SubmitFrameResampled(float* samples) {
  if (!xe_audio_queue_paused(ring_.get())) {
    // Up to 768 frames (the media player's stereo) in up to 6 channels.
    float out[768 * 6];
    const size_t frames = channel_samples_;
    const size_t out_ch = out_channels_;
    float* stereo = out;
    const float* prepared = out;
    if (out_ch == 6) {
      // 5.1 as the frontend takes it, FL FR C LFE SL SR interleaved - the
      // order the console's channels already are in.
      if (channels_ == 6 && need_format_conversion_) {
        conversion::sequential_6_BE_to_interleaved_6_LE(out, samples,
                                                        kChannelSamplesDefault);
      } else if (channels_ == 6) {
        prepared = samples;
      } else {
        // Stereo music on the front pair.
        for (size_t i = 0; i < frames; ++i) {
          float* f = &out[i * 6];
          f[0] = samples[i * 2];
          f[1] = samples[i * 2 + 1];
          f[2] = f[3] = f[4] = f[5] = 0.0f;
        }
      }
    } else if (channels_ == 6 && need_format_conversion_) {
      conversion::sequential_6_BE_to_interleaved_2_LE(stereo, samples,
                                                      kChannelSamplesDefault);
    } else if (channels_ == 6) {
      // Interleaved little-endian 5.1 (FL FR C LFE SL SR), folded to stereo.
      for (size_t i = 0; i < frames; ++i) {
        const float* f = &samples[i * 6];
        const float c = f[2] * 0.7071f;
        stereo[i * 2] = f[0] + c + f[4] * 0.7071f;
        stereo[i * 2 + 1] = f[1] + c + f[5] * 0.7071f;
      }
    } else {
      prepared = samples;
    }

    const float volume = xe_audio_queue_get_volume(ring_.get());
    if (volume != 1.0f) {
      for (size_t i = 0; i < frames * out_ch; ++i) {
        out[i] = prepared[i] * volume;
      }
      prepared = out;
    }

    if (frequency_ == 48000)
      xe_audio_queue_write(ring_.get(), prepared, frames * out_ch);
    else
      Resample(prepared, frames);
  }

  xe_audio_queue_finish_packet(ring_.get());
}

void LibretroAudioDriver::Pause() { xe_audio_queue_pause(ring_.get(), 1); }
void LibretroAudioDriver::Resume() { xe_audio_queue_pause(ring_.get(), 0); }
void LibretroAudioDriver::SetVolume(float volume) { xe_audio_queue_volume(ring_.get(), volume); }

// ---- LibretroAudioSystem --------------------------------------------------

LibretroAudioSystem::LibretroAudioSystem(cpu::Processor* processor, xe_audio_registry* registry)
    : AudioSystem(processor) {
  group_.reset(xe_audio_group_new_frontend(registry));
}

LibretroAudioSystem::~LibretroAudioSystem() = default;

void LibretroAudioSystem::StopWorker() {
  // Cancel frontend-dependent writes before upstream joins the audio worker.
  CancelPendingOutput();
  AudioSystem::StopWorker();
}

void LibretroAudioSystem::CancelPendingOutput() {
  xe_audio_group_stop(group_.get());
}
X_STATUS LibretroAudioSystem::CreateDriver(size_t index,
                                            xe::threading::Semaphore* semaphore,
                                            AudioDriver** out_driver) {
  // An audio client: 5.1, big-endian, channel after channel, at 48 kHz.
  auto driver = new LibretroAudioDriver(this, semaphore, 48000, 6, true,
      &clients_[index].frames_processed);
  if (!driver->Initialize()) {
    delete driver;
    return X_STATUS_UNSUCCESSFUL;
  }
  *out_driver = driver;
  return X_STATUS_SUCCESS;
}

AudioDriver* LibretroAudioSystem::CreateDriver(
    xe::threading::Semaphore* semaphore, uint32_t frequency,
    uint32_t channels, bool need_format_conversion) {
  // The media player's (XMP background music), in its own format - taking it
  // as a client's 5.1 big-endian frames was the static in game menus.
  return new LibretroAudioDriver(this, semaphore, frequency, channels,
                                 need_format_conversion);
}

void LibretroAudioSystem::DestroyDriver(AudioDriver* driver) {
  if (driver) {
    driver->Shutdown();
    delete driver;
  }
}

}  // namespace libretro
}  // namespace apu
}  // namespace xe
