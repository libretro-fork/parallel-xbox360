/* Published host submission index, not GPU completion. The backend must still
 * wait for its own completion primitive before reading GPU-written contents. */
#ifndef XE_CORE_SUBMISSION_COUNTER_H
#define XE_CORE_SUBMISSION_COUNTER_H
#include <stdint.h>
#include <retro_atomic.h>
#ifndef RETRO_ATOMIC_HAS_64
#error Supported core targets require libretro-common 64-bit atomic operations
#endif
typedef retro_atomic_64_t xe_submission_counter;
static INLINE uint64_t xe_submission_acquire(const xe_submission_counter *counter) {
  return (uint64_t)retro_atomic_load_acquire_64(counter);
}
static INLINE void xe_submission_publish(xe_submission_counter *counter, uint64_t value) {
  retro_atomic_store_release_64(counter, (int64_t)value);
}
#endif
