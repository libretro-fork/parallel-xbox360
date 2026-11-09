/* Instruction-local raw storage identity. MSVC C89; no retained value cache.
 * Caller lends immutable operand metadata until this lookup returns. */
#ifndef XE_CORE_SHADER_OPERAND_LEND_H
#define XE_CORE_SHADER_OPERAND_LEND_H
#include <stdint.h>
#include "../third_party/libretro-common/include/retro_inline.h"
typedef struct xe_shader_operand_key {
   uint32_t source, index, addressing;
} xe_shader_operand_key;
typedef int (*xe_shader_operand_key_read)(const void *, unsigned,
      xe_shader_operand_key *);
/* Return an earlier operand with identical storage, or index itself. The
 * supported ALU width is at most three operands. Modifiers/swizzles are applied
 * after loading and deliberately are not part of raw storage identity. */
static INLINE unsigned xe_shader_operand_lend_index(const void *operands,
      unsigned index, xe_shader_operand_key_read read)
{
   xe_shader_operand_key wanted, prior;
   unsigned i;
   if (!index || index >= 3 || !operands || !read ||
         !read(operands, index, &wanted)) return index;
   for (i = 0; i < index; ++i) {
      if (read(operands, i, &prior) && prior.source == wanted.source &&
            prior.index == wanted.index && prior.addressing == wanted.addressing)
         return i;
   }
   return index;
}
#endif
