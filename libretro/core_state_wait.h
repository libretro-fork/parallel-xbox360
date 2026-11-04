#ifndef XE_CORE_STATE_WAIT_H
#define XE_CORE_STATE_WAIT_H
#include <stdint.h>
#include <retro_inline.h>

enum { XE_STATE_WAIT_NONE = 0, XE_STATE_WAIT_DELAY = 1 };
typedef struct xe_state_wait {
   uint32_t kind;
   uint64_t remaining_ms;
} xe_state_wait;

static INLINE uint64_t xe_state_relative_millis(int64_t ticks)
{
   /* Unsigned magnitude is defined even for INT64_MIN. */
   return ticks < 0 ? ((uint64_t)0 - (uint64_t)ticks) / 10000 : 0;
}

static INLINE int xe_state_wait_valid(const xe_state_wait *wait)
{
   return wait && ((wait->kind == XE_STATE_WAIT_NONE && !wait->remaining_ms) ||
         (wait->kind == XE_STATE_WAIT_DELAY && wait->remaining_ms <= UINT32_MAX));
}
static INLINE int xe_state_wait_capture_delay(xe_state_wait *wait,
      uint64_t now_ms, uint64_t deadline_ms)
{
   uint64_t remaining = deadline_ms > now_ms ? deadline_ms - now_ms : 0;
   if (!wait || remaining > UINT32_MAX) return 0;
   wait->kind = XE_STATE_WAIT_DELAY;
   wait->remaining_ms = remaining;
   return 1;
}
static INLINE int xe_state_wait_rebase(const xe_state_wait *wait,
      uint64_t now_ms, uint64_t *deadline_ms)
{
   if (!deadline_ms || !xe_state_wait_valid(wait) ||
         wait->remaining_ms > UINT64_MAX - now_ms) return 0;
   *deadline_ms = now_ms + wait->remaining_ms;
   return 1;
}
#endif
