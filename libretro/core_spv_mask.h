/* Borrowed scalar mask iteration: least-significant set bit first. Neither
 * pointer escapes the call; empty masks leave the index unchanged. */
#ifndef XE_CORE_SPV_MASK_H
#define XE_CORE_SPV_MASK_H
#include <stdint.h>
#include <retro_inline.h>
#include <compat/intrinsics.h>
static INLINE int xe_spv_mask_take(uint32_t *remaining, uint32_t *index) {
   uint32_t value = *remaining;
   if (!value) return 0;
   *index = (uint32_t)compat_ctz(value);
   *remaining = value & (value - 1);
   return 1;
}
#endif
