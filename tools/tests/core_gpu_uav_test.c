/* Dense physical-group scoreboard, transitions and aliases; MSVC C89. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../libretro/core_gpu_uav.c"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %u: %s\n",(unsigned)__LINE__,#x); exit(1); } } while (0)
typedef char uav_state_size_check[(sizeof(xe_gpu_uav) == 268) ? 1 : -1];
static uint32_t seed = 7;
static uint32_t random_word(void) { seed = seed * 1664525u + 1013904223u; return seed; }
static int write_span(xe_gpu_uav *s, uint32_t first, uint32_t length, uint32_t base) {
   return xe_gpu_uav_prepare(s, 8, 1, first, length, base);
}
static void boundaries(void) {
   xe_gpu_uav s; unsigned i, phase;
   memset(&s, 0xcd, sizeof(s)); xe_gpu_uav_init(&s, 64);
   CHECK(!write_span(&s, 0, 32, 0));
   CHECK(s.resource_state == 8 && s.count == 1 && s.spans[0].first == 0 && s.spans[0].end == 128);
   CHECK(write_span(&s, 64, 32, 0)); /* Disjoint raw bytes, same expanded group. */
   CHECK(!write_span(&s, 128, 128, 0));
   CHECK(s.count == 1 && s.spans[0].end == 256);
   CHECK(!xe_gpu_uav_prepare(&s, 64, 0, 0, 0, 0) && !s.count && s.resource_state == 64);
   CHECK(!write_span(&s, XE_GPU_UAV_APERTURE - 32, 32, 0));
   CHECK(write_span(&s, XE_GPU_UAV_APERTURE - 1, 1, 0));
   CHECK(write_span(&s, UINT32_MAX - 7, 16, 0) && s.unknown);
   CHECK(write_span(&s, 256, 32, 0) && !s.unknown);
   CHECK(write_span(&s, 0, 0, 0) && s.unknown);
   CHECK(!xe_gpu_uav_prepare(&s, 64, 0, 0, 0, 0) && !s.unknown);
   CHECK(write_span(&s, 127, 16, 128) && s.unknown);
   CHECK(write_span(&s, XE_GPU_UAV_APERTURE - 8, 8, 1) && s.unknown);
   for (phase = 0; phase < 128; ++phase) {
      xe_gpu_uav_init(&s, 64);
      CHECK(!write_span(&s, phase + 512, 32, phase));
      CHECK(write_span(&s, phase + 576, 32, phase));
      CHECK(s.spans[0].first == phase + 512 && s.spans[0].end == phase + 640);
      CHECK(!write_span(&s, phase + 640, 128, phase));
   }
   xe_gpu_uav_init(&s, 64);
   CHECK(!write_span(&s, 128, 32, 0));
   CHECK(write_span(&s, 256, 32, 64)); /* Different base phases share GPU bytes. */
   xe_gpu_uav_clear(&s); CHECK(s.resource_state == 8);
   CHECK(!write_span(&s, 0, 128, 0));
   CHECK(!write_span(&s, 256, 128, 0));
   CHECK(!write_span(&s, 128, 128, 0));
   CHECK(s.count == 1 && s.spans[0].first == 0 && s.spans[0].end == 384);
   CHECK(write_span(&s, 7, 1, 0));
   xe_gpu_uav_clear(&s);
   for (i = 0; i < XE_GPU_UAV_SPANS; ++i) CHECK(!write_span(&s, i * 256, 128, 0));
   CHECK(s.count == XE_GPU_UAV_SPANS);
   CHECK(write_span(&s, XE_GPU_UAV_SPANS * 256, 128, 0) && s.count == 1);
   xe_gpu_uav_clear(&s);
   for (i = 0; i < 100000; ++i) CHECK(!write_span(&s, i * 128, 128, 0));
   CHECK(s.count == 1 && s.spans[0].end == 12800000);
}
static void traces(void) {
   xe_gpu_uav s;
   unsigned char pending[4096];
   unsigned sequence, i, j, n, transitions = 0, barriers = 0, disjoint = 0;
   int old_uav = 0;
   xe_gpu_uav_init(&s, 64); memset(pending, 0, sizeof(pending));
   for (sequence = 0; sequence < 1000000; ++sequence) {
      uint32_t base = (random_word() >> 6) % 128;
      uint32_t start = 256 + (random_word() >> 4) % (sizeof(pending) - 512);
      uint32_t length = (random_word() >> 9) % 64 + 1;
      int next_uav = (random_word() >> 15) % 5 != 0;
      int alias = (random_word() >> 20) % 37 == 0;
      int hazard = 0, decision;
      if (alias) { xe_gpu_uav_clear(&s); memset(pending, 0, sizeof(pending)); }
      if (old_uav != next_uav) { memset(pending, 0, sizeof(pending)); ++transitions; }
      /* For every guest byte, enumerate its complete resolution-expanded
       * 128-byte physical group. Dense bytes independently check the compact
       * interval representation, including shifted base phases. */
      if (next_uav) for (i = start; i < start + length; ++i) {
         uint32_t group = base + ((i - base) / 128) * 128;
         for (j = 0; j < 128; ++j) if (pending[group + j]) hazard = 1;
      }
      decision = xe_gpu_uav_prepare(&s, next_uav ? 8 : 64, next_uav, start, length, base);
      CHECK(s.resource_state == (next_uav ? 8u : 64u));
      CHECK(!hazard || decision);
      if (decision) { memset(pending, 0, sizeof(pending)); ++barriers; }
      else if (next_uav) ++disjoint;
      if (next_uav) for (i = start; i < start + length; ++i) {
         uint32_t group = base + ((i - base) / 128) * 128;
         memset(pending + group, 1, 128);
      }
      for (n = 0; n < s.count; ++n) {
         CHECK(s.spans[n].first < s.spans[n].end && s.spans[n].end <= sizeof(pending));
         for (i = s.spans[n].first; i < s.spans[n].end; ++i) CHECK(pending[i]);
      }
      for (i = 0; i < sizeof(pending); ++i) if (pending[i]) {
         int found = 0;
         for (n = 0; n < s.count; ++n) if (i >= s.spans[n].first && i < s.spans[n].end) found = 1;
         CHECK(found);
      }
      old_uav = next_uav;
   }
   CHECK(transitions && barriers && disjoint);
   printf("UAV epochs: 1000000 group-footprint operations, %u transitions, %u barriers, %u independent writes; byte scoreboard passed\n", transitions, barriers, disjoint);
}
int main(void) { boundaries(); traces(); return 0; }
