/* Final sample boundary, finite/nonfinite inputs and bounds; MSVC C89. */
#include <stdio.h>
#include <string.h>
#include "core_audio_samples.h"

static int round_trip(void) {
  float input[256];
  int16_t output[256];
  int base, i;
  for (base = -32768; base < 32768; base += 256) {
    for (i = 0; i < 256; ++i) input[i] = (float)(base + i) / 32768.0f;
    xe_audio_samples_int16(output, input, 256);
    for (i = 0; i < 256; ++i) if (output[i] != base + i) return 0;
  }
  return 1;
}

static int rounding(void) {
  static const float lsb[] = {
    0.25f, 0.5f, 0.75f, 1.5f, 2.5f, 3.5f,
    -0.25f, -0.5f, -0.75f, -1.5f, -2.5f, -3.5f,
    32766.5f, 32767.5f, -32767.5f
  };
  static const int16_t expected[] = {0, 0, 1, 2, 2, 4, 0, 0, -1, -2, -2, -4,
                                    32766, 32767, -32768};
  static const uint32_t near_tie[] = {0x377fffffu, 0x37800001u,
                                     0xb77fffffu, 0xb7800001u};
  float input[15];
  int16_t output[15];
  unsigned i;
  for (i = 0; i < 15; ++i) input[i] = lsb[i] / 32768.0f;
  xe_audio_samples_int16(output, input, 15);
  for (i = 0; i < 15; ++i) if (output[i] != expected[i]) return 0;
  memcpy(input, near_tie, sizeof(near_tie));
  xe_audio_samples_int16(output, input, 4);
  return output[0] == 0 && output[1] == 1 && output[2] == 0 && output[3] == -1;
}

int main(void) {
  static const uint32_t bits[] = {
    0x00000000u, 0x80000000u, 0x3f000000u, 0xbf000000u,
    0x3f800000u, 0xbf800000u, 0x40000000u, 0xc0000000u,
    0x7f800000u, 0xff800000u, 0x7fc00000u, 0xffc00000u
  };
  static const float expected[] = {0, 0, 0.5f, -0.5f, 1, -1, 1, -1, 1, -1, 0, 0};
  static const int16_t integers[] = {0, 0, 16384, -16384, 32767, -32768,
                                   32767, -32768, 32767, -32768, 0, 0};
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
  if (!round_trip() || !rounding()) return 3;
  puts("PASS: normalization, all 65536 PCM round trips, nearest-even ties and bounds");
  return 0;
}
