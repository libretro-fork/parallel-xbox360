#ifndef XE_CORE_AUDIO_CONVERT_H
#define XE_CORE_AUDIO_CONVERT_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Borrowed input; offset/count are frames, planar stride remains total_frames.
 * Inputs/outputs must not overlap. Valid channel counts are 2 and 6; BE is 6.
 * The queue validates these constraints before invoking this converter. */
void xe_audio_convert(float *out, const float *in, size_t total_frames,
      size_t offset, size_t count, unsigned channels, unsigned output_channels,
      int sequential_be, float volume);
#ifdef __cplusplus
}
#endif
#endif
