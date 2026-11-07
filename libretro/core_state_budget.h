/* One bounded state-operation budget; no guest clock or pacing. MSVC C89. */
#ifndef XE_CORE_STATE_BUDGET_H
#define XE_CORE_STATE_BUDGET_H
#include <stdint.h>
#include <retro_inline.h>
typedef struct xe_state_budget { int64_t start,limit; } xe_state_budget;
static INLINE int xe_state_budget_begin(xe_state_budget *out,int64_t now,int64_t limit) {
  if(!out || limit<0) return 0;
  out->start=now;out->limit=limit;return 1;
}
static INLINE int64_t xe_state_budget_left(const xe_state_budget *budget,int64_t now) {
  uint64_t elapsed;
  if(!budget || budget->limit<0 || now<budget->start) return 0;
  elapsed=(uint64_t)now-(uint64_t)budget->start;
  return elapsed>=(uint64_t)budget->limit?0:budget->limit-(int64_t)elapsed;
}
#endif
