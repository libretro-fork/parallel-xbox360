/* Output values and planar stride across arbitrary queue-span boundaries. C89. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "core_audio_convert.h"
#define FRAMES 257
static float input[FRAMES * 6], expected[FRAMES * 6], output[FRAMES * 6 + 2];
static float value(size_t frame, unsigned channel) {
   return ((float)(frame % 31) - 15.0f) * (float)(channel + 1) / 32.0f;
}
static void big_endian(float *destination, float sample) {
   uint32_t bits;
   unsigned char *bytes = (unsigned char *)destination;
   memcpy(&bits, &sample, 4);
   bytes[0] = (unsigned char)(bits >> 24); bytes[1] = (unsigned char)(bits >> 16);
   bytes[2] = (unsigned char)(bits >> 8); bytes[3] = (unsigned char)bits;
}
static int check(unsigned channels, unsigned out_channels, int be, float volume) {
   size_t i, offset, count, chunk;
   unsigned c;
   float center, low, left, right;
   for (i = 0; i < FRAMES; ++i) {
      for (c = 0; c < channels; ++c) {
         if (be) big_endian(input + c * FRAMES + i, value(i, c));
         else input[i * channels + c] = value(i, c);
      }
      if (channels == out_channels) {
         for (c = 0; c < out_channels; ++c) expected[i * out_channels + c] = value(i, c);
      } else if (channels == 2) {
         expected[i * 6] = value(i, 0); expected[i * 6 + 1] = value(i, 1);
         for (c = 2; c < 6; ++c) expected[i * 6 + c] = 0.0f;
      } else if (be) {
         center = value(i, 2) * 0.707106781f; low = value(i, 3) * 0.5f;
         left = value(i, 4) * 0.707106781f; right = value(i, 5) * 0.707106781f;
         expected[i * 2] = value(i, 0) + (center + (left + low));
         expected[i * 2 + 1] = value(i, 1) + (center + (right + low));
      } else {
         center = value(i, 2) * 0.7071067811865475244f;
         expected[i * 2] = value(i, 0) + center + value(i, 4) * 0.7071067811865475244f;
         expected[i * 2 + 1] = value(i, 1) + center + value(i, 5) * 0.7071067811865475244f;
      }
   }
   if (volume != 1.0f)
      for (i = 0; i < FRAMES * out_channels; ++i) expected[i] *= volume;
   for (chunk = 1; chunk <= FRAMES; ++chunk) {
      output[0] = output[FRAMES * out_channels + 1] = 1234.0f;
      for (offset = 0; offset < FRAMES; offset += count) {
         count = FRAMES - offset;
         if (count > chunk) count = chunk;
         xe_audio_convert(output + 1 + offset * out_channels, input, FRAMES,
               offset, count, channels, out_channels, be, volume);
      }
      if (output[0] != 1234.0f || output[FRAMES * out_channels + 1] != 1234.0f ||
            memcmp(output + 1, expected, FRAMES * out_channels * sizeof(float))) {
         fprintf(stderr, "conversion mismatch: %u -> %u BE=%d volume=%g chunk=%lu\n",
               channels, out_channels, be, (double)volume, (unsigned long)chunk);
         return 0;
      }
   }
   return 1;
}
static int payload_test(void) {
   static const uint32_t payloads[6] = {UINT32_C(0x80000000), 0,
      UINT32_C(0x7fc12345), UINT32_C(0xffc54321), 1, UINT32_C(0x7f800000)};
   float raw[6], result[6];
   uint32_t actual[6];
   unsigned c;
   unsigned char *bytes;
   for (c = 0; c < 6; ++c) {
      bytes = (unsigned char *)&raw[c];
      bytes[0] = (unsigned char)(payloads[c] >> 24);
      bytes[1] = (unsigned char)(payloads[c] >> 16);
      bytes[2] = (unsigned char)(payloads[c] >> 8);
      bytes[3] = (unsigned char)payloads[c];
   }
   xe_audio_convert(result, raw, 1, 0, 1, 6, 6, 1, 1.0f);
   memcpy(actual, result, sizeof(actual));
   return !memcmp(actual, payloads, sizeof(actual));
}

static int media_gain_test(void) {
   float raw[18], result[6];
   uint32_t actual[6];
   static const uint32_t expected_bits[6] = {
      UINT32_C(0x3f3504f3), UINT32_C(0x3f3504f3),
      UINT32_C(0x3f3504f3), 0, 0, UINT32_C(0x3f3504f3)
   };
   memset(raw, 0, sizeof(raw));
   raw[2] = raw[10] = raw[17] = 1.0f;
   xe_audio_convert(result, raw, 3, 0, 3, 6, 2, 0, 1.0f);
   memcpy(actual, result, sizeof(actual));
   if (memcmp(actual, expected_bits, sizeof(actual))) {
      fputs("media downmix gain loses float precision\n", stderr);
      return 0;
   }
   return 1;
}

int main(void) {
   static const float volumes[] = {1.0f, 0.375f, 0.0f};
   unsigned v, in, out;
   if (!payload_test() || !media_gain_test()) return 2;
   for (v = 0; v < 3; ++v)
      for (in = 2; in <= 6; in += 4)
         for (out = 2; out <= 6; out += 4)
            if (!check(in, out, 0, volumes[v]) ||
                  (in == 6 && !check(in, out, 1, volumes[v]))) return 1;
   puts("PASS: exact native audio conversion, volume, planar strides, all 257 span sizes and output bounds");
   return 0;
}
