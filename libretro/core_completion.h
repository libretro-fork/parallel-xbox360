/* Owner arms/cancels; workers finish generation-tagged tickets. MSVC C89.
 * Storage remains alive through worker joins. Zero is never a pending ticket.
 * Tickets must not outlive 2^63 rearms of the same owner. */
#ifndef XE_CORE_COMPLETION_H
#define XE_CORE_COMPLETION_H
#include <stdint.h>
#include <retro_atomic.h>
#if !defined(RETRO_ATOMIC_HAS_64)
#error Completion ownership requires real 64-bit atomics
#endif
typedef struct xe_completion { retro_atomic_64_t state; } xe_completion;
static void xe_completion_init(xe_completion *c) { retro_atomic_64_init(&c->state,0); }
static uint64_t xe_completion_current(const xe_completion *c) {
  return (uint64_t)retro_atomic_load_acquire_64(&c->state);
}
static uint64_t xe_completion_begin(xe_completion *c) {
  uint64_t old,next;
  do {
    old=xe_completion_current(c);next=((old+UINT64_C(2))|UINT64_C(1));
  } while(!retro_atomic_cas_64(&c->state,(int64_t)old,(int64_t)next));
  return next;
}
static int xe_completion_finish(xe_completion *c,uint64_t ticket) {
  return (ticket&1) && retro_atomic_cas_64(&c->state,(int64_t)ticket,(int64_t)(ticket&~UINT64_C(1)));
}
static int xe_completion_pending(const xe_completion *c,uint64_t ticket) {
  return (ticket&1) && xe_completion_current(c)==ticket;
}
static void xe_completion_cancel(xe_completion *c) {
  uint64_t ticket=xe_completion_begin(c);
  xe_completion_finish(c,ticket);
}
#endif
