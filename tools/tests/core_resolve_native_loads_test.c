/* Coordinate and arbitrary-bit equivalence of native direct-resolve load reuse.
 * The scalar reference follows each original shader pixel independently. C89. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "core_resolve_native_loads.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %u: %s\n", (unsigned)__LINE__, #x); exit(1); } } while (0)
static uint32_t seed = 0x94812abc;
static uint32_t random_word(void) {
  seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed;
}
static uint32_t reference_x(uint32_t source, uint32_t dest, uint32_t i,
    uint32_t scale, int fill) {
  uint32_t limit = fill ? scale / 2 : 0, x = dest + i;
  uint32_t bias = i < scale / 2 && x < limit ? limit - x : 0;
  return (source + i + bias) / scale;
}
static uint32_t texel_bits(uint32_t x, uint32_t y, uint32_t sample, uint32_t word) {
  /* Deliberately includes arbitrary NaNs, denormals, integer and depth bits.
   * Same coordinates must feed the unchanged format packer the same bits. */
  uint32_t value = x * 0x19283745u + y * 0x77733311u + sample * 97 + word;
  value ^= value >> 13; value *= 0x9876a543u; return value ^ (value >> 7);
}
static uint64_t cases, pixels_checked, loads_original, loads_reused;
static void one_run(uint32_t run, uint32_t scale, uint32_t source, uint32_t dest,
    uint32_t y, uint32_t sample, int fill, int native_scaled) {
  uint32_t i, word, words = run == 4 ? 2 : 1;
  uint32_t original[8], shared[8];
  for (i = 0; i < run; ++i) {
    uint32_t load = xe_resolve_native_load_index(run, scale, native_scaled, i);
    uint32_t x = reference_x(source, dest, i, scale, fill);
    CHECK(load <= i && xe_resolve_native_load_index(run, scale, native_scaled, load) == load);
    ++loads_original; if (load == i) ++loads_reused;
    if (load != i) CHECK(x == reference_x(source, dest, load, scale, fill));
    for (word = 0; word < words; ++word) {
      original[i * words + word] = texel_bits(x, y, sample, word);
      shared[i * words + word] = load == i ? original[i * words + word] : shared[load * words + word];
      CHECK(shared[i * words + word] == original[i * words + word]);
    }
    ++pixels_checked;
  }
  ++cases;
}
static void directed(void) {
  uint32_t run, scale, msaa, phase, column, tile, offset, fill;
  for (run = 4; run <= 8; run *= 2) for (scale = 1; scale <= 32; ++scale)
  for (msaa = 0; msaa < 3; ++msaa) {
    uint32_t tile_width = ((run == 4 ? 40u : 80u) * scale) >> (msaa == 2);
    for (phase = 0; phase < 2 * scale; phase += run)
    for (column = 0; column < 4; ++column) for (tile = 0; tile < 2; ++tile)
    for (offset = 0; offset < 16; ++offset) for (fill = 0; fill < 2; ++fill) {
      uint32_t x = phase % tile_width;
      one_run(run, scale, column * tile_width + x, tile * tile_width + x - offset * 8 * scale,
          column + phase, msaa, fill, 1);
    }
  }
  CHECK(xe_resolve_native_load_index(8, 4, 1, 0) == 0);
  CHECK(xe_resolve_native_load_index(8, 4, 1, 7) == 4);
  CHECK(xe_resolve_native_load_index(4, 4, 1, 3) == 0);
  for (scale = 0; scale < 64; ++scale) for (phase = 0; phase < 8; ++phase) {
    CHECK(xe_resolve_native_load_index(8, scale, 0, phase) == phase);
    CHECK(xe_resolve_native_load_index(5, scale, 1, phase) == phase);
    if (!scale || scale > 16 || (scale & (scale - 1)))
      CHECK(xe_resolve_native_load_index(8, scale, 1, phase) == phase);
  }
}
static void randomized(void) {
  uint32_t i;
  for (i = 0; i < 1000000; ++i) {
    uint32_t run = random_word() & 1 ? 4 : 8, scale = random_word() % 32 + 1;
    uint32_t msaa = random_word() % 3;
    uint32_t width = ((run == 4 ? 40u : 80u) * scale) >> (msaa == 2);
    uint32_t x = (random_word() % (width / run)) * run;
    uint32_t source = random_word() * width + x;
    uint32_t dest = random_word() * width + x - (random_word() & 15) * 8 * scale;
    uint32_t y = random_word(), sample = random_word() & 3;
    one_run(run, scale, source, dest, y, sample, (int)(random_word() & 1), 1);
  }
}
int main(void) {
  directed(); randomized();
  CHECK(cases < UINT32_MAX && pixels_checked < UINT32_MAX);
  printf("native resolve equivalence: %lu runs, %lu pixels; %lu original / %lu retained loads (mixed-scale fixtures)\n",
      (unsigned long)cases, (unsigned long)pixels_checked,
      (unsigned long)loads_original, (unsigned long)loads_reused);
  return 0;
}
