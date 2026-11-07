/* Interpreter loop ownership and aL addressing. MSVC C89. */
#ifndef XE_CORE_SHADER_LOOP_H
#define XE_CORE_SHADER_LOOP_H
#include <stdint.h>
#include <retro_inline.h>
/* depth counts live entries; slot depth is private until a nonempty push. */
static INLINE int xe_shader_loop_enter(uint32_t *depth, uint32_t constants[4],
      uint32_t iterators[4], uint32_t constant, int repeat)
{
   uint32_t slot = *depth;
   if (slot >= 4) return 0;
   constants[slot] = constant;
   if (!repeat) iterators[slot] = 0;
   if (iterators[slot] >= (constant & 255)) return 0;
   *depth = slot + 1;
   return 1;
}
static INLINE int32_t xe_shader_loop_address(uint32_t depth,
      const uint32_t constants[4], const uint32_t iterators[4])
{
   uint32_t constant;
   int32_t step;
   int64_t address;
   if (!depth || depth > 4) return 0;
   constant = constants[depth - 1];
   step = (int32_t)((constant >> 16) & 255);
   if (step >= 128) step -= 256;
   /* Wide intermediate also avoids UB if a caller supplies a damaged iterator. */
   address = (int64_t)iterators[depth - 1] * step + ((constant >> 8) & 255);
   if (address > 256) return 256;
   if (address < -256) return -256;
   return (int32_t)address;
}
#endif
