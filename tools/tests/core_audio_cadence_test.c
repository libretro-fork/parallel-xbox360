/* Fractional audio cadence tests, MSVC C89. */
#include <stdio.h>
#include <math.h>
#include "core_audio_cadence.h"
static int check_rate(double fps, unsigned runs) {
  xe_audio_cadence clock;
  double total = 0.0, ideal;
  unsigned i;
  size_t frames;
  xe_audio_cadence_reset(&clock);
  for (i = 0; i < runs; ++i) {
    frames = xe_audio_cadence_next(&clock, 48000, fps);
    total += (double)frames;
    ideal = ((double)i + 1.0) * 48000.0 / fps;
    if (fabs(total - ideal) > 1.000001 || clock.fraction < 0.0 || clock.fraction >= 1.0) {
      fprintf(stderr, "cadence drift at %.9f Hz run %u\n", fps, i); return 0;
    }
  }
  return 1;
}
int main(void) {
  static const double rates[] = {50.0, 60.0, 60000.0 / 1001.0, 59.94, 120.0, 144.0, 165.0, 240.0};
  xe_audio_cadence clock;
  unsigned i;
  for (i = 0; i < sizeof(rates) / sizeof(rates[0]); ++i)
    if (!check_rate(rates[i], 100000)) return 1;
  xe_audio_cadence_reset(&clock);
  if (xe_audio_cadence_next(&clock, 48000, 0.0) != 800 ||
      xe_audio_cadence_next(&clock, 48000, -1.0) != 800 ||
      xe_audio_cadence_next(&clock, 48000, HUGE_VAL) != 800) return 2;
  (void)xe_audio_cadence_next(&clock, 48000, 165.0);
  xe_audio_cadence_reset(&clock);
  if (clock.fraction != 0.0 || xe_audio_cadence_next(&clock, 48000, 60.0) != 800) return 3;
  puts("PASS: 800000 fractional-cadence runs, invalid rates and reset");
  return 0;
}
