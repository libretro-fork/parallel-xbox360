/* Allocation-free interval analysis vs the original fixed-point edge walk.
 * Raw packed CF fixtures and independent reference, MSVC C89. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libretro/core_shader_reentry.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"reentry line %u\n",(unsigned)__LINE__); exit(1); } } while (0)
#define LIMIT 8194
static uint32_t words[LIMIT / 2 * 3 + 2];
static uint64_t writes[LIMIT], result[LIMIT + 2], expected[LIMIT];
static uint32_t sources[LIMIT], targets[LIMIT];
static uint32_t seed = UINT32_C(0x17e146ab);
static uint32_t random_word(void)
{
   seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
   return seed;
}
static void pack(size_t index, uint32_t low, unsigned opcode)
{
   uint32_t *p = words + index / 2 * 3;
   if (index & 1) { p[1] |= low << 16; p[2] = (low >> 16) | (opcode << 28); }
   else { p[0] = low; p[1] = opcode << 12; }
}
/* Keep the original order-dependent bound scan and repeated overlapping-edge
 * walk, not the new interval component algorithm. No result helper is reused. */
static size_t reference(size_t word_count)
{
   size_t bound = word_count / 3, pair, half, edges = 0;
   size_t edge, label, first, last, i, next_first, next_last;
   uint32_t low, high, opcode;
   int changed;
   const uint32_t *p;
   for (pair = 0; pair < bound; ++pair) {
      p = words + pair * 3;
      for (half = 0; half < 2; ++half) {
         low = half ? (p[1] >> 16) | (p[2] << 16) : p[0];
         high = half ? p[2] >> 16 : p[1] & 65535u;
         opcode = high >> 12;
         if (opcode == 1 || opcode == 2 || opcode == 3 || opcode == 4 ||
               opcode == 5 || opcode == 6 || opcode == 13 || opcode == 14) {
            if ((low & 4095u) < bound) bound = low & 4095u;
         }
         if ((opcode == 8 || opcode == 11) && (low & 8191u) <= pair * 2 + half) {
            CHECK(edges < LIMIT);
            sources[edges] = (uint32_t)(pair * 2 + half);
            targets[edges++] = low & 8191u;
         }
      }
   }
   memset(expected, 0, sizeof(expected));
   for (edge = 0; edge < edges; ++edge) {
      label = targets[edge]; if (label >= bound * 2) continue;
      first = last = label;
      do {
         changed = 0;
         for (i = 0; i < edges; ++i) {
            if (targets[i] <= last && sources[i] >= first) {
               next_first = first < targets[i] ? first : targets[i];
               next_last = last > sources[i] ? last : sources[i];
               if (next_first != first || next_last != last) changed = 1;
               first = next_first; last = next_last;
            }
         }
      } while (changed);
      if (last >= bound * 2) last = bound * 2 - 1;
      for (i = first; i <= last; ++i) expected[label] |= writes[i];
   }
   return bound * 2;
}
static void compare(size_t word_count)
{
   size_t count = reference(word_count), i;
   memset(result, 0, sizeof(result));
   result[0] = UINT64_C(0xab12345687654321);
   result[count + 1] = UINT64_C(0x21765432123456ab);
   CHECK(xe_shader_reentry_analyze(words, word_count, writes, result + 1, count));
   CHECK(result[0] == UINT64_C(0xab12345687654321));
   CHECK(result[count + 1] == UINT64_C(0x21765432123456ab));
   for (i = 0; i < count; ++i) CHECK(result[i + 1] == expected[i]);
}
int main(void)
{
   size_t trial, count, i;
   uint32_t op, target;
   for (i = 0; i < LIMIT; ++i) writes[i] = UINT64_C(1) << (i % 64);
   CHECK(xe_shader_reentry_analyze(NULL, 0, NULL, NULL, 0));
   CHECK(!xe_shader_reentry_analyze(NULL, 3, writes, result, 2));
   CHECK(!xe_shader_reentry_analyze(words, 3, NULL, result, 2));
   CHECK(!xe_shader_reentry_analyze(words, 3, writes, NULL, 2));
   CHECK(!xe_shader_reentry_analyze(words, 3, writes, result, 1));
   CHECK(!xe_shader_reentry_analyze(words, 3, writes, result, 4));
   CHECK(xe_shader_reentry_all(writes, 64) == UINT64_MAX);
   CHECK(xe_shader_reentry_all(NULL, 0) == 0);
   /* Nested, crossing, touching, adjacent, self and duplicate destinations. */
   memset(words, 0, sizeof(words));
   pack(3, 0, 11); pack(5, 3, 8); pack(9, 6, 11);
   pack(10, 10, 8); pack(11, 6, 8); pack(15, 12, 11);
   compare(24);
   /* EXEC in the high half shrinks the bound below the current pair. The
    * earlier backward edge still affects the valid prefix's write union. */
   memset(words, 0, sizeof(words)); pack(4, 0, 11); pack(5, 1, 2);
   compare(12);
   /* Full 13-bit target, source just past that domain, and trailing words. */
   memset(words, 0, sizeof(words)); pack(8191, 8191, 11); pack(8193, 8191, 8);
   compare(LIMIT / 2 * 3 + 2);
   for (trial = 0; trial < 30000; ++trial) {
      count = 2 + random_word() % 126;
      count &= ~(size_t)1;
      memset(words, 0, count / 2 * 3 * sizeof(*words));
      for (i = 0; i < count; ++i) {
         op = random_word() % 16;
         target = random_word() % (count + 12);
         pack(i, target, op);
         writes[i] = ((uint64_t)random_word() << 32) | random_word();
      }
      compare(count / 2 * 3);
   }
   /* Long crossing chain and all-self loops exercise bounded linear finish. */
   memset(words, 0, sizeof(words));
   for (i = 1; i < 256; ++i) pack(i, i > 2 ? (uint32_t)i - 2 : 0, 8);
   compare(384);
   memset(words, 0, sizeof(words));
   for (i = 0; i < LIMIT; ++i) pack(i, (uint32_t)i & 8191u, 11);
   compare(LIMIT / 2 * 3);
   puts("PASS: 30005 packed reentry graphs match fixed-point oracle; guards, shrinking bounds, 13-bit targets, empty and invalid spans");
   return 0;
}
