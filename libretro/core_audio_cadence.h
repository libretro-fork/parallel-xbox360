/* Per-run audio accounting. MSVC C89. */
#ifndef PARALLEL_XBOX360_AUDIO_CADENCE_H
#define PARALLEL_XBOX360_AUDIO_CADENCE_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xe_audio_cadence {
  double fraction;
} xe_audio_cadence;
void xe_audio_cadence_reset(xe_audio_cadence *clock);
size_t xe_audio_cadence_next(xe_audio_cadence *clock, unsigned sample_rate,
                           double frame_rate);
#ifdef __cplusplus
}
#endif
#endif
