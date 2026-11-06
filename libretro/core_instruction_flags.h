/* Monotonic mapped instruction facts. Flags are hints, not publication of other
 * objects; lifetime/ready-state synchronization stays with those objects. */
#ifndef XE_CORE_INSTRUCTION_FLAGS_H
#define XE_CORE_INSTRUCTION_FLAGS_H
#include <stdint.h>
#include <retro_atomic.h>
typedef retro_atomic_int_t xe_ic_flags;
enum {
  XE_IC_FLAG_RESOLVED = 1,
  XE_IC_FLAG_MMIO = 2,
  XE_IC_FLAG_SYSCALL = 4,
  XE_IC_FLAG_RETURN_SITE = 8
};
static INLINE uint32_t xe_ic_flags_load(const xe_ic_flags *slot) {
  return slot ? (uint32_t)retro_atomic_load_relaxed_int(slot) : 0;
}
static INLINE int xe_ic_flags_test(const xe_ic_flags *slot, uint32_t mask) {
  return (xe_ic_flags_load(slot) & mask) != 0;
}
static INLINE void xe_ic_flags_set(xe_ic_flags *slot, uint32_t mask) {
  if (slot) retro_atomic_fetch_or_int(slot, (int32_t)mask);
}
#endif
