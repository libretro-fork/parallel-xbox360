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
  float scaled, fraction;
  int value;
  for (i = 0; i < count; ++i) {
    /* Frontends normalize signed PCM by 32768. Match that scale so every
     * representable int16 value survives a float round trip exactly. */
    scaled = normalized(samples[i]) * 32768.0f;
    if (scaled >= 32767.0f) { out[i] = 32767; continue; }
    if (scaled <= -32768.0f) { out[i] = -32768; continue; }
    value = (int)scaled;
    fraction = scaled - (float)value;
    /* Nearest, ties to even, independent of the host rounding mode. Avoid
     * adding 0.5f, which can round a value just below a tie up to that tie. */
    if (fraction > 0.5f || (fraction == 0.5f && (value & 1))) ++value;
    if (fraction < -0.5f || (fraction == -0.5f && (value & 1))) --value;
    out[i] = (int16_t)value;
  }
}
