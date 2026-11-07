/* Compare clipped launches with the actual unsigned shader predicate; C89. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "core_resolve_dispatch.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %u: %s\n",(unsigned)__LINE__,#x); exit(1); } } while (0)
static uint32_t seed = 1;
static uint32_t random_word(void) { seed = seed * 1664525u + 1013904223u; return seed; }
/* Independent scalar port of the SPIR-V/DXIL tile/run mapping and uint compare.
 * A passing thread writes its entire run, even beyond the rectangle edge. */
static int shader_axis(uint32_t thread, uint32_t pixels_per_thread,
      uint32_t tile, uint32_t tile_pixels, uint32_t offset_div8,
      uint32_t size_div8, uint32_t scale) {
   uint32_t run = thread * pixels_per_thread;
   uint32_t shader_tile = tile + run / tile_pixels;
   uint32_t in_tile = run % tile_pixels;
   uint32_t dest = shader_tile * tile_pixels + in_tile - offset_div8 * 8 * scale;
   return dest < size_div8 * 8 * scale;
}
static void axis_case(uint32_t old, uint32_t thread_pixels, uint32_t tile,
      uint32_t tile_pixels, uint32_t offset, uint32_t size, uint32_t scale) {
   uint32_t clipped = xe_resolve_dispatch_axis(old, thread_pixels == 8 ? 6 : (thread_pixels == 4 ? 5 : 3),
         tile * tile_pixels, offset, size, scale);
   uint32_t thread, valid_old = 0, valid_new = 0;
   CHECK(clipped <= old);
   for (thread = 0; thread < old * 8; ++thread) {
      if (shader_axis(thread, thread_pixels, tile, tile_pixels, offset, size, scale)) {
         ++valid_old;
         CHECK(thread < clipped * 8);
         if (thread < clipped * 8) ++valid_new;
      }
   }
   CHECK(valid_old == valid_new);
}
static void axis_tests(void) {
   unsigned i, scale, msaa, wide, offset, size, tiles;
   for (scale = 1; scale <= 16; ++scale) {
      for (wide = 0; wide < 2; ++wide) for (msaa = 0; msaa < 3; ++msaa) {
         uint32_t tile_x = ((wide ? 40 : 80) >> (msaa == 2)) * scale;
         uint32_t tile_y = (16 >> (msaa >= 1)) * scale;
         uint32_t pixels = wide ? 4 : 8;
         for (offset = 0; offset <= 9; ++offset) for (size = 0; size <= 33; ++size) for (tiles = 1; tiles <= 3; ++tiles) {
            uint32_t old_x = (tiles * tile_x + pixels * 8 - 1) / (pixels * 8);
            uint32_t old_y = (tiles * tile_y + 7) / 8;
            axis_case(old_x, pixels, 0, tile_x, offset, size, scale);
            axis_case(old_y, 1, 0, tile_y, offset & 1, size, scale);
         }
      }
   }
   for (i = 0; i < 100000; ++i) {
      uint32_t scale_x = random_word() % 16 + 1;
      uint32_t pixels = random_word() & 1 ? 4 : 8;
      uint32_t tile = random_word() % 32;
      uint32_t tile_pixels = (random_word() & 1 ? 20 : 80) * scale_x;
      uint32_t groups = random_word() % 33;
      uint32_t offset_x = random_word() % 10, width = random_word() % 1024;
      axis_case(groups, pixels, tile, tile_pixels, offset_x, width, scale_x);
   }
   /* No wrap-based shader behavior is silently simplified. */
   CHECK(xe_resolve_dispatch_axis(10, 6, UINT32_MAX - 10, 0, 8, 4) == 10);
   CHECK(xe_resolve_dispatch_axis(UINT32_MAX, 31, 0, 0, 8, 4) == UINT32_MAX);
   CHECK(xe_resolve_dispatch_axis(10, 6, 0, 0, UINT32_MAX, 16) == 10);
   CHECK(xe_resolve_dispatch_axis(10, 6, 0, 0, 8, 0) == 10);
   CHECK(xe_resolve_dispatch_axis(10, 6, 0, 16, 8, 4) == 10);
   CHECK(xe_resolve_dispatch_axis(10, 32, 0, 0, 8, 4) == 10);
   CHECK(xe_resolve_dispatch_axis(0, 6, 0, 0, 8, 4) == 0);
   CHECK(xe_resolve_dispatch_axis(10, 6, 0, 1, 0, 4) == 0);
   /* Nominal tile coverage may miss a live thread in the rounded final group.
    * Preserve it, including its original whole vector write beyond the edge. */
   CHECK(xe_resolve_dispatch_axis(2, 6, 0, 10, 1, 1) == 2);
   CHECK(xe_resolve_dispatch_axis(2, 6, 0, 0, 9, 1) == 2);
}
static uint64_t launch_hash(uint32_t groups_x, uint32_t groups_y,
      uint32_t tile_x, uint32_t tile_y, uint32_t tile_pixels_x, uint32_t tile_pixels_y,
      uint32_t offset_x, uint32_t offset_y, uint32_t width, uint32_t height,
      uint32_t scale_x, uint32_t scale_y, uint32_t pixels, uint32_t *runs) {
   uint32_t x, y, p; uint64_t hash = 7;
   *runs = 0;
   for (y = 0; y < groups_y * 8; ++y) for (x = 0; x < groups_x * 8; ++x) {
      if (!shader_axis(x, pixels, tile_x, tile_pixels_x, offset_x, width, scale_x) ||
            !shader_axis(y, 1, tile_y, tile_pixels_y, offset_y, height, scale_y)) continue;
      ++*runs;
      for (p = 0; p < pixels; ++p) {
         /* Include all samples in every legacy vector run, with coordinates
          * serving as a unique source/output tag. Ordering is unchanged. */
         hash = hash * 31 + (tile_x * tile_pixels_x + x * pixels + p);
         hash = hash * 31 + (tile_y * tile_pixels_y + y);
      }
   }
   return hash;
}
static void rectangle_tests(void) {
   unsigned i;
   for (i = 0; i < 3000; ++i) {
      uint32_t sx = random_word() % 16 + 1, sy = random_word() % 16 + 1;
      uint32_t wide = (random_word() >> 2) & 1, msaa = random_word() % 3;
      uint32_t tx = random_word() % 5, ty = random_word() % 9;
      uint32_t px = ((wide ? 40 : 80) >> (msaa == 2)) * sx;
      uint32_t py = (16 >> (msaa >= 1)) * sy, ppt = wide ? 4 : 8;
      uint32_t ox = random_word() % 10, oy = random_word() % 2;
      uint32_t width = random_word() % 65, height = random_word() % 65;
      uint32_t gx = (px * (random_word() % 3 + 1) + ppt * 8 - 1) / (ppt * 8);
      uint32_t gy = (py * (random_word() % 3 + 1) + 7) / 8;
      uint32_t nx = xe_resolve_dispatch_axis(gx, ppt == 4 ? 5 : 6, tx * px, ox, width, sx);
      uint32_t ny = xe_resolve_dispatch_axis(gy, 3, ty * py, oy, height, sy);
      uint32_t old_runs, new_runs;
      uint64_t old_hash = launch_hash(gx, gy, tx, ty, px, py, ox, oy, width, height, sx, sy, ppt, &old_runs);
      uint64_t new_hash = launch_hash(nx, ny, tx, ty, px, py, ox, oy, width, height, sx, sy, ppt, &new_runs);
      CHECK(old_hash == new_hash && old_runs == new_runs);
   }
}
int main(void) {
   uint32_t gx, gy;
   axis_tests(); rectangle_tests();
   gx = xe_resolve_dispatch_axis(5, 6, 0, 0, 1, 4);
   gy = xe_resolve_dispatch_axis(8, 3, 0, 0, 1, 4);
   CHECK(gx == 1 && gy == 4);
   /* An overlong launch is clipped without changing live runs. */
   CHECK(xe_resolve_dispatch_axis(20, 6, 0, 0, 40, 4) == 20);
   CHECK(xe_resolve_dispatch_axis(96, 3, 0, 0, 22, 4) == 88);
   /* Height is specified in multiples of eight; 184 guest pixels need 92
    * groups rather than the 96 covering twelve complete EDRAM tile rows. */
   CHECK(xe_resolve_dispatch_axis(96, 3, 0, 0, 23, 4) == 92);
   CHECK(xe_resolve_dispatch_axis(80, 6, 0, 0, 160, 4) == 80);
   CHECK(xe_resolve_dispatch_axis(360, 3, 0, 0, 90, 4) == 360);
   printf("resolve dispatch: scalar shader equivalence, vector tails, scales 1..16, MSAA, 32/64bpp and overflow pass; 4x 8x8 fixture: 40 -> %lu groups\n", (unsigned long)(gx * gy));
   return 0;
}
