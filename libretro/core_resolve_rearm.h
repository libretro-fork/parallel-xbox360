/* Resolve watch handoff. Mark/begin use the caller's existing fault lock;
 * the GPU thread alone consumes/finishes a detached bitmap. The owner stays
 * at one address; shallow copies would invalidate its internal pointers. */
#ifndef XE_CORE_RESOLVE_REARM_H
#define XE_CORE_RESOLVE_REARM_H
#include "core_memexport.h"
typedef struct xe_resolve_rearm {
  xe_memexport_map maps[2];
  xe_memexport_map *pending, *borrowed;
} xe_resolve_rearm;
/* Cold init or reset after callback unregister/drain; invalidates all leases. */
static INLINE void xe_resolve_rearm_reset(xe_resolve_rearm *s) {
  xe_memexport_reset(s->maps);
  xe_memexport_reset(s->maps + 1);
  s->pending = s->maps; s->borrowed = NULL;
}
/* Union at guest-page granularity. No bound depends on the number of owners,
 * duplicates or pending ranges: both maps cover the entire physical aperture. */
static INLINE int xe_resolve_rearm_mark(xe_resolve_rearm *s, uint32_t base, uint32_t size) {
  return xe_memexport_mark(s->pending, base, size);
}
/* Pointer exchange only; the returned cursor borrows the detached map until
 * finish/reset. A second begin while borrowed is rejected without mutation. */
static INLINE int xe_resolve_rearm_begin(xe_resolve_rearm *s, xe_memexport_cursor *cursor) {
  xe_memexport_map *map = s->pending;
  if (s->borrowed || !map->any) return 0;
  s->borrowed = map;
  s->pending = map == s->maps ? s->maps + 1 : s->maps;
  return xe_memexport_window(map, 0, XE_MEMEXPORT_BUFFER_SIZE, cursor);
}
/* No mark can touch this map while it is borrowed. Clear only its touched
 * word interval; other words were zero from initialization or their last use. */
static INLINE void xe_resolve_rearm_finish(xe_resolve_rearm *s) {
  xe_memexport_map *map = s->borrowed;
  if (!map) return;
  if (map->any) {
    uint32_t first = map->first >> 6, last = map->last >> 6;
    memset(map->words + first, 0, (size_t)(last - first + 1) * sizeof(map->words[0]));
  }
  map->first = XE_MEMEXPORT_PAGE_COUNT; map->last = 0; map->any = 0;
  s->borrowed = NULL;
}
#ifdef __cplusplus
struct xe_resolve_rearm_storage : xe_resolve_rearm {
  xe_resolve_rearm_storage() { xe_resolve_rearm_reset(this); }
  xe_resolve_rearm_storage(const xe_resolve_rearm_storage&) = delete;
  xe_resolve_rearm_storage& operator=(const xe_resolve_rearm_storage&) = delete;
};
#endif
#endif
