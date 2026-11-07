/* Stable native binary publication/ownership. MSVC C89; BSD license. */
#ifndef XE_CORE_BINARY_SLOT_H
#define XE_CORE_BINARY_SLOT_H
#include <stddef.h>
#include <retro_atomic.h>
#include <retro_inline.h>
#ifndef RETRO_ATOMIC_HAS_PTR
#error Native binary publication requires atomic pointer operations
#endif
typedef struct xe_binary_slot {
   retro_atomic_ptr_t value;
} xe_binary_slot;
typedef struct xe_binary_snapshot {
   void *binary;
   int ready;
} xe_binary_snapshot;
static INLINE void xe_binary_slot_init(xe_binary_slot *slot)
{
   retro_atomic_ptr_init(&slot->value, NULL);
}
/* Slots never move. Their own address marks a completed failure, without an
 * invalid pointer constant or a separately allocated failure token. */
static INLINE xe_binary_snapshot xe_binary_slot_read(const xe_binary_slot *slot)
{
   xe_binary_snapshot result = {NULL, 0};
   void *value;
   if (!slot) return result;
   value = retro_atomic_load_acquire_ptr(&slot->value);
   result.ready = value != NULL;
   if ((const void *)value != (const void *)slot) result.binary = value;
   return result;
}
/* Transfers ownership only on success. NULL publishes a cached failure. A
 * losing publisher still owns its binary. Readers borrow until joined reset. */
static INLINE int xe_binary_slot_publish(xe_binary_slot *slot, void *binary)
{
   if (!slot || binary == (void *)slot) return 0;
   return retro_atomic_cas_ptr(&slot->value, NULL, binary ? binary : (void *)slot);
}
/* All publishers/readers joined, and native users released, before taking the
 * owned binary. Also resets cached failure; repeated take returns NULL. */
static INLINE void *xe_binary_slot_take(xe_binary_slot *slot)
{
   void *value;
   if (!slot) return NULL;
   value = retro_atomic_exchange_ptr(&slot->value, NULL);
   return value == (void *)slot ? NULL : value;
}
#endif
