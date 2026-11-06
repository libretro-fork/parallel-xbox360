/* Single-owner CPU snapshots. Generate directly into the inactive typed slot,
 * then commit by pointer swap. Slots must stay at stable addresses, be equally
 * sized, initialized including compared padding, and exclusively owned through
 * compare/commit. Moving the owning object requires reinitializing the pair. */
#ifndef XE_CORE_SNAPSHOT_PAIR_H
#define XE_CORE_SNAPSHOT_PAIR_H
#include <stddef.h>
#include <string.h>
#include <retro_inline.h>
typedef struct xe_snapshot_pair {
  void *active, *candidate;
  int valid;
} xe_snapshot_pair;
/* Neither data slot needs an initial value. */
static INLINE void xe_snapshot_pair_init(xe_snapshot_pair *s,
    void *first, void *second) {
  s->active = first; s->candidate = second; s->valid = 0;
}
static INLINE int xe_snapshot_pair_commit(xe_snapshot_pair *s, size_t slot_bytes) {
  void *old_active;
  if (s->valid && !memcmp(s->active, s->candidate, slot_bytes)) return 0;
  old_active = s->active; s->active = s->candidate; s->candidate = old_active;
  s->valid = 1;
  return 1;
}
#endif
