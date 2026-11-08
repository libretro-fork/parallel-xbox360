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
                                         const xe_apu_credit_sink* credits)
    : group_(system ? system->group() : nullptr),
      registry_required_(system != nullptr),
      semaphore_(semaphore),
      frequency_(frequency ? frequency : 48000),
      channels_(channels == 2 ? 2 : 6),
      // Native XAudio submits 256 frames of six channels. The existing media
      // caller separately submits 1536 interleaved floats; this is its legacy
      // chunk contract, not a hardware stereo quantum or retro_run cadence.
      channel_samples_(channels == 2 ? 768 : kChannelSamplesDefault),
      out_channels_(LibretroAudioSystem::output_channels()),
      need_format_conversion_(need_format_conversion) {
  if (credits) credits_ = *credits;
}

LibretroAudioDriver::~LibretroAudioDriver() {
  Shutdown();
}

bool LibretroAudioDriver::Initialize() {
  if (ring_) return true;
  ring_.reset(registry_required_ ? xe_audio_group_queue_new(group_)
                                : xe_audio_queue_new(out_channels_));
  if (!ring_) {
    ring_.reset();
    return false;
  }
  group_ = nullptr;
  return true;
}

void LibretroAudioDriver::Shutdown() { xe_audio_queue_stop(ring_.get()); }

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
  if (frequency_ != 48000) {
    SubmitFrameResampled(samples, channel_samples_);
    return;
  }
  if (!xe_audio_queue_paused(ring_.get())) {
    xe_audio_queue_write_converted(ring_.get(), samples, channel_samples_,
        channels_, channels_ == 6 && need_format_conversion_, xe_audio_queue_get_volume(ring_.get()));
  }
  if (credits_.owner) xe_apu_clients_return_credit(&credits_);
  else if (semaphore_) semaphore_->Release(1, nullptr);
}

bool LibretroAudioDriver::SubmitInterleavedFrames(const float* samples, uint32_t frames) {
  if (need_format_conversion_ || !samples || !frames || frames > channel_samples_) return false;
  if (frequency_ != 48000) {
    SubmitFrameResampled(samples, frames);
    return true;
  }
  if (!xe_audio_queue_paused(ring_.get()))
    xe_audio_queue_write_converted(ring_.get(), samples, frames, channels_, 0,
                                   xe_audio_queue_get_volume(ring_.get()));
  if (semaphore_) semaphore_->Release(1, nullptr);
  return true;
}

void LibretroAudioDriver::SubmitFrameResampled(const float* samples, size_t frames) {
  if (!xe_audio_queue_paused(ring_.get())) {
    // Up to 768 frames (the media player's stereo) in up to 6 channels.
    float out[768 * 6];
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
        const float c = f[2] * 0.7071067811865475244f;
        stereo[i * 2] = f[0] + c + f[4] * 0.7071067811865475244f;
        stereo[i * 2 + 1] = f[1] + c + f[5] * 0.7071067811865475244f;
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

  // Return one client credit, or the media player's legacy completion token.
  if (credits_.owner) xe_apu_clients_return_credit(&credits_);
  else if (semaphore_) {
    semaphore_->Release(1, nullptr);
  }
}

void LibretroAudioDriver::Pause() { xe_audio_queue_pause(ring_.get(), 1); }
void LibretroAudioDriver::Resume() { xe_audio_queue_pause(ring_.get(), 0); }
void LibretroAudioDriver::SetVolume(float volume) { xe_audio_queue_volume(ring_.get(), volume); }

bool LibretroAudioDriver::CaptureState(xe_audio_driver_view* out) const {
  xe_audio_driver_view view = {};
  if (!xe_audio_driver_storage_external(out, sizeof(*out), this, sizeof(*this)) ||
      !xe_audio_queue_checkpoint_storage_valid(ring_.get(), out, sizeof(*out))) return false;
  view.version = 1;
  view.frequency = frequency_;
  view.channels = channels_;
  view.output_channels = out_channels_;
  view.channel_frames = channel_samples_;
  view.planar_be = need_format_conversion_;
  view.resample_position = resample_pos_;
  std::memcpy(view.last, last_, sizeof(last_));
  if (!xe_audio_queue_capture(ring_.get(), &view.queue)) return false;
  *out = view;
  return true;
}

bool LibretroAudioDriver::CanRestoreState(const xe_audio_driver_view* view) const {
  return xe_audio_driver_storage_external(view, sizeof(*view), this, sizeof(*this)) &&
      xe_audio_queue_checkpoint_storage_valid(ring_.get(), view, sizeof(*view)) &&
      xe_audio_driver_view_valid(view, frequency_, channels_, out_channels_,
      channel_samples_, need_format_conversion_) &&
      (!view->queue.bytes[0] || xe_audio_driver_storage_external(view->queue.data[0],
          view->queue.bytes[0], this, sizeof(*this))) &&
      (!view->queue.bytes[1] || xe_audio_driver_storage_external(view->queue.data[1],
          view->queue.bytes[1], this, sizeof(*this))) &&
      xe_audio_queue_can_restore(ring_.get(), &view->queue);
}

bool LibretroAudioDriver::RestoreState(const xe_audio_driver_view* view) {
  if (!CanRestoreState(view) || !xe_audio_queue_restore(ring_.get(), &view->queue)) return false;
  resample_pos_ = view->resample_position;
  std::memcpy(last_, view->last, sizeof(last_));
  return true;
}

// ---- LibretroAudioSystem --------------------------------------------------

LibretroAudioSystem::LibretroAudioSystem(cpu::Processor* processor, xe_audio_registry* registry)
    : AudioSystem(processor) {
  group_.reset(xe_audio_group_new(registry));
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
                                            const xe_apu_credit_sink* credits,
                                            AudioDriver** out_driver) {
  // An audio client: 5.1, big-endian, channel after channel, at 48 kHz.
  auto driver = new LibretroAudioDriver(this, nullptr, 48000, 6, true, credits);
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
