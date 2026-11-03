/* Final libretro sample-format boundary; MSVC C89. */
#ifndef XE_CORE_AUDIO_SAMPLES_H
#define XE_CORE_AUDIO_SAMPLES_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void xe_audio_samples_clamp(float *samples, size_t count);
void xe_audio_samples_int16(int16_t *out, const float *samples, size_t count);
#ifdef __cplusplus
}
#endif
#endif
