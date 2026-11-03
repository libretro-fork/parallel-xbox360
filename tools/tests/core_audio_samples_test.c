/* Final sample boundary, finite/nonfinite inputs and bounds; MSVC C89. */
#include <stdio.h>
#include <string.h>
#include "core_audio_samples.h"

int main(void) {
  static const uint32_t bits[] = {
    0x00000000u, 0x80000000u, 0x3f000000u, 0xbf000000u,
    0x3f800000u, 0xbf800000u, 0x40000000u, 0xc0000000u,
    0x7f800000u, 0xff800000u, 0x7fc00000u, 0xffc00000u
  };
  static const float expected[] = {0, 0, 0.5f, -0.5f, 1, -1, 1, -1, 1, -1, 0, 0};
  static const int16_t integers[] = {0, 0, 16383, -16383, 32767, -32767,
                                   32767, -32767, 32767, -32767, 0, 0};
  float samples[14];
  int16_t out[14];
  unsigned i;
  samples[0] = samples[13] = 99.0f;
  out[0] = out[13] = 1234;
  memcpy(samples + 1, bits, sizeof(bits));
  xe_audio_samples_int16(out + 1, samples + 1, 12);
  xe_audio_samples_clamp(samples + 1, 12);
  for (i = 0; i < 12; ++i)
    if (samples[i + 1] != expected[i] || out[i + 1] != integers[i]) return 1;
  if (samples[0] != 99.0f || samples[13] != 99.0f ||
      out[0] != 1234 || out[13] != 1234) return 2;
  xe_audio_samples_clamp(NULL, 0);
  xe_audio_samples_int16(NULL, NULL, 0);
  puts("PASS: float normalization, legacy integer boundary and output bounds");
  return 0;
}
