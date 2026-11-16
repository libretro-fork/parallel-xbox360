/* Interpreter loop ownership and aL addressing. MSVC C89. */
#ifndef XE_CORE_SHADER_LOOP_H
#define XE_CORE_SHADER_LOOP_H
#include <stdint.h>
#include <string.h>
#include <retro_inline.h>
#define XE_SHADER_LOOP_UNBOUNDED_ADVANCE 1
/* depth counts live entries; slot depth is private until a nonempty push. */
static INLINE int xe_shader_loop_enter(uint32_t *depth, uint32_t constants[4],
      uint32_t remaining[4], int32_t addresses[4], uint32_t constant, int repeat)
{
   uint32_t slot = *depth;
   if (slot >= 4 || !(constant & 255)) return 0;
   addresses[slot] = repeat ? (slot ? addresses[slot - 1] : 0)
         : (int32_t)((constant >> 8) & 255);
   constants[slot] = constant;
   remaining[slot] = constant & 255;
   *depth = slot + 1;
   return 1;
}
static INLINE int32_t xe_shader_loop_address(uint32_t depth,
      const int32_t addresses[4])
{
   int32_t address;
   if (!depth || depth > 4) return 0;
   address = addresses[depth - 1];
   /* Clamp the computed operand index, not the progress inherited by REPEAT. */
   if (address > 256) return 256;
   if (address < -256) return -256;
   return address;
}
/* Return whether LOOP_END branches back. A repeat inherits aL, not a stale
 * iterator in a reused slot. Preserve raw progress through excursions beyond
 * the operand-index range; xe_shader_loop_address applies that range on read.
 * The unsigned addition also gives defined behavior for damaged state. */
static INLINE int xe_shader_loop_advance(uint32_t *depth,
      const uint32_t constants[4], uint32_t remaining[4],
      int32_t addresses[4], int stop)
{
   uint32_t slot;
   int32_t step;
   uint32_t address;
   if (!*depth || *depth > 4) return 0;
   slot = *depth - 1;
   if (remaining[slot] <= 1 || stop) { *depth = slot; return 0; }
   --remaining[slot];
   step = (int32_t)((constants[slot] >> 16) & 255);
   if (step >= 128) step -= 256;
   address = (uint32_t)addresses[slot] + (uint32_t)step;
   /* Bit transfer avoids signed overflow and an out-of-range signed cast. */
   memcpy(&addresses[slot], &address, sizeof(address));
   return 1;
}
#endif
