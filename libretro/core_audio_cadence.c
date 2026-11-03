#include "core_audio_cadence.h"

void xe_audio_cadence_reset(xe_audio_cadence *clock) {
  clock->fraction = 0.0;
}

size_t xe_audio_cadence_next(xe_audio_cadence *clock, unsigned sample_rate,
                           double frame_rate) {
  double exact;
  size_t frames;
  /* Reject NaN/infinity as well as invalid rates before an integer cast. */
  if (!(frame_rate >= 1.0 && frame_rate <= 1000000.0)) frame_rate = 60.0;
  exact = (double)sample_rate / frame_rate + clock->fraction;
  frames = (size_t)exact;
  clock->fraction = exact - (double)frames;
  return frames;
}
