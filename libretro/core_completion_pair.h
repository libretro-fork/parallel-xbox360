/* Two-party retirement: initialize before publishing either participant. */
#ifndef XE_CORE_COMPLETION_PAIR_H
#define XE_CORE_COMPLETION_PAIR_H
#include <retro_atomic.h>
#include <retro_inline.h>
typedef struct xe_completion_pair {
   retro_atomic_int_t remaining;
} xe_completion_pair;
static INLINE void xe_completion_pair_init(xe_completion_pair *pair) {
   retro_atomic_int_init(&pair->remaining, 2);
}
/* Exactly one release per participant. Only the last may recycle storage. */
static INLINE int xe_completion_pair_release(xe_completion_pair *pair) {
   return retro_atomic_fetch_sub_int(&pair->remaining, 1) == 1;
}
#endif
