/* Append-only native address interning. MSVC C89-compatible.
 * Slots never move or change after their first successful publication.
 * UINT64_MAX is an empty marker, not an executable host address.
 * Initialize before users start and destroy only after they have stopped.
 */
#ifndef XE_CORE_ADDRESS_TABLE_H
#define XE_CORE_ADDRESS_TABLE_H
#include <stdint.h>
#include <stddef.h>
#include <retro_atomic.h>
#include <retro_inline.h>
static INLINE void xe_address_table_init(retro_atomic_64_t *slots, size_t count) {
   size_t i;
   for (i = 0; i < count; ++i) retro_atomic_64_init(&slots[i], -1);
}
/* Returns count on exhaustion/invalid address. Contenders for an identical
 * address converge on the same first slot, without a count reservation hole.
 * The value is the entire payload; relaxed probes need no secondary acquire. */
static INLINE size_t xe_address_table_intern(retro_atomic_64_t *slots,
      size_t count, uint64_t address) {
   size_t i;
   uint64_t current;
   if (address == UINT64_MAX) return count;
   for (i = 0; i < count; ++i) {
      current = (uint64_t)retro_atomic_load_relaxed_64(&slots[i]);
      if (current == address) return i;
      if (current != UINT64_MAX) continue;
      if (retro_atomic_cas_64(&slots[i], -1, (int64_t)address)) return i;
      if ((uint64_t)retro_atomic_load_relaxed_64(&slots[i]) == address) return i;
   }
   return count;
}
#endif
