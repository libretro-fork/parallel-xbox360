/* Static direct-resolve load sharing. MSVC C89; no GPU branch or new storage. */
#ifndef XE_CORE_RESOLVE_NATIVE_LOADS_H
#define XE_CORE_RESOLVE_NATIVE_LOADS_H
#include <stdint.h>
/* A direct run starts at a multiple of its 4/8-pixel width. Its source tile
 * width and the resolve origin are multiples of scale_x. For power-of-two
 * scales, each aligned min(scale_x, run_width) segment therefore reads one
 * native source texel. Half-pixel fill stays inside that same guest pixel.
 * Other scales/layouts keep their existing individual loads. */
static uint32_t xe_resolve_native_load_index(uint32_t run_width,
    uint32_t scale_x, int native_source_in_scaled_dest, uint32_t pixel) {
  uint32_t share;
  if (!native_source_in_scaled_dest || (run_width != 4 && run_width != 8) ||
      pixel >= run_width || !scale_x || scale_x > 16 || (scale_x & (scale_x - 1)))
    return pixel;
  share = scale_x < run_width ? scale_x : run_width;
  return pixel & ~(share - 1);
}
#endif
