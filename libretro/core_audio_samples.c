#include "core_audio_samples.h"

static float normalized(float value) {
  /* An invalid guest sample must not reach an integer cast or the frontend. */
  if (value != value) return 0.0f;
  if (value > 1.0f) return 1.0f;
  if (value < -1.0f) return -1.0f;
  return value;
}

void xe_audio_samples_clamp(float *samples, size_t count) {
  size_t i;
  for (i = 0; i < count; ++i) samples[i] = normalized(samples[i]);
}

void xe_audio_samples_int16(int16_t *out, const float *samples, size_t count) {
  size_t i;
  for (i = 0; i < count; ++i)
    out[i] = (int16_t)(normalized(samples[i]) * 32767.0f);
}
