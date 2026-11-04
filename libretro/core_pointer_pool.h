/* Bounded unordered ownership transfer. MSVC C89-compatible.
 * The pool owns each published pointer until exactly one take succeeds.
 * No node dereference before ownership transfer, hence no freelist ABA/UAF.
 * Overflow remains the caller's property. Quiesce users before draining/freeing.
 */
#ifndef XE_CORE_POINTER_POOL_H
#define XE_CORE_POINTER_POOL_H
#include <stddef.h>
#include <retro_atomic.h>
#include <retro_inline.h>
#define XE_POINTER_POOL_CAPACITY 64
typedef struct xe_pointer_pool {
   retro_atomic_ptr_t slots[XE_POINTER_POOL_CAPACITY];
} xe_pointer_pool;
static INLINE void xe_pointer_pool_init(xe_pointer_pool *pool) {
   unsigned i;
   for (i = 0; i < XE_POINTER_POOL_CAPACITY; ++i)
      retro_atomic_ptr_init(&pool->slots[i], NULL);
}
/* Payload initialization precedes put; successful take acquires those writes.
 * A relaxed probe only avoids pointless RMW traffic on empty/occupied slots. */
static INLINE int xe_pointer_pool_put(xe_pointer_pool *pool, void *value) {
   unsigned i;
   if (!value) return 0;
   for (i = 0; i < XE_POINTER_POOL_CAPACITY; ++i)
      if (!retro_atomic_load_relaxed_ptr(&pool->slots[i]) &&
            retro_atomic_cas_ptr(&pool->slots[i], NULL, value)) return 1;
   return 0;
}
static INLINE void *xe_pointer_pool_take(xe_pointer_pool *pool) {
   unsigned i;
   void *value;
   for (i = 0; i < XE_POINTER_POOL_CAPACITY; ++i) {
      if (!retro_atomic_load_relaxed_ptr(&pool->slots[i])) continue;
      value = retro_atomic_exchange_ptr(&pool->slots[i], NULL);
      if (value) return value;
   }
   return NULL;
}
#endif
