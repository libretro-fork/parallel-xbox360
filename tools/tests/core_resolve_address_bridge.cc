/* Native CPU-address ABI adapter only. Algorithms/tests live in C89. */
#include <cstdint>
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-value" /* Existing release assert macros. */
#endif
#include "xenia/gpu/texture_address.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
extern "C" uint32_t xe_test_native_address(unsigned volume, uint32_t x,
    uint32_t y, uint32_t pitch, uint32_t slice, uint32_t height, unsigned bpp) {
  return volume ? uint32_t(xe::gpu::texture_address::Tiled3D(
      int32_t(x), int32_t(y), int32_t(slice), pitch << 5, height << 5, bpp)) :
      uint32_t(xe::gpu::texture_address::Tiled2D(int32_t(x),int32_t(y),pitch << 5,bpp));
}
