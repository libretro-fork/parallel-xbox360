/* Live relaunch state only. Resource lifetime remains serialized by its owner.
 * Request producers may be concurrent; all supported core hosts have native
 * lock-free 64-bit common atomics. No callbacks, allocation or waiting here. */
#ifndef XE_CORE_LAUNCH_STATE_H
#define XE_CORE_LAUNCH_STATE_H
#include <stdint.h>
#include <string.h>
#include <retro_atomic.h>
#include <retro_inline.h>
#ifndef RETRO_ATOMIC_HAS_64
#error Relaunch state requires libretro-common 64-bit atomics
#endif
typedef struct xe_launch_state {
  retro_atomic_64_t generation;
  retro_atomic_int_t active;
} xe_launch_state;

static INLINE void xe_launch_state_init(xe_launch_state *state) {
  retro_atomic_store_relaxed_64(&state->generation, 0);
  retro_atomic_store_relaxed_int(&state->active, 0);
}
static INLINE uint64_t xe_launch_state_latest(const xe_launch_state *state) {
  return (uint64_t)retro_atomic_load_acquire_64(&state->generation);
}
static INLINE uint64_t xe_launch_state_request(xe_launch_state *state) {
  int64_t previous, next;
  uint64_t bits;
  do {
    previous = retro_atomic_load_acquire_64(&state->generation);
    bits = (uint64_t)previous + UINT64_C(1);
    /* Preserve the full unsigned ticket domain without signed overflow or
     * an out-of-range unsigned-to-signed arithmetic conversion. */
    memcpy(&next, &bits, sizeof(next));
  } while (!retro_atomic_cas_64(&state->generation, previous, next));
  /* This is a cold control transition. Publish before taking the launch lock
   * or allowing teardown to observe a superseding request. */
  retro_atomic_thread_fence_seq_cst();
  return bits;
}
static INLINE int xe_launch_state_active(const xe_launch_state *state) {
  return retro_atomic_load_acquire_int(&state->active) != 0;
}
static INLINE void xe_launch_state_set_active(xe_launch_state *state, int active) {
  retro_atomic_exchange_int(&state->active, active != 0);
  /* Keep resource teardown/rebuild after flag publication, including on weak
   * hosts. Readers use acquire; the existing launch owner still joins workers. */
  retro_atomic_thread_fence_seq_cst();
}
#endif
