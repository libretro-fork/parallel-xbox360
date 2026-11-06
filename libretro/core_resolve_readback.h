/* Resolve page keys are atomic hints, not publication of GPU completion.
 * A fault scan captures each key once into final, exclusively owned records.
 * Native submission/fence waits and the caller's fault serialization remain.
 * Plans borrow records until the next capture/reset/destruction. */
#ifndef XE_CORE_RESOLVE_READBACK_H
#define XE_CORE_RESOLVE_READBACK_H
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <retro_atomic.h>
#include "core_memexport.h"
#ifndef RETRO_ATOMIC_HAS_64
#error Resolve readback requires libretro-common 64-bit atomic operations
#endif
#ifndef XE_RESOLVE_ALLOC
#define XE_RESOLVE_ALLOC malloc
#define XE_RESOLVE_FREE free
#endif
#define XE_RESOLVE_MAX_RUNS ((XE_MEMEXPORT_PAGE_COUNT + 1) / 2)
/* Vulkan validates this wire layout against VkBufferCopy at the API boundary.
 * Byte access preserves the native record type's aliasing and object lifetime. */
#define XE_RESOLVE_NATIVE_STRIDE 24
typedef char xe_resolve_key_width[sizeof(retro_atomic_64_t) == 8 ? 1 : -1];
typedef char xe_resolve_compact_width[sizeof(xe_memexport_run) == 8 ? 1 : -1];
typedef struct xe_resolve_readback {
  retro_atomic_64_t *owners;
  void *records;
  uint32_t stride;
} xe_resolve_readback;
typedef struct xe_resolve_copy_plan {
  const void *records;
  uint32_t count, bytes, stride;
} xe_resolve_copy_plan;
typedef struct xe_resolve_copy_cursor {
  const unsigned char *records;
  uint32_t remaining, stride;
} xe_resolve_copy_cursor;
/* A P-page ownership selection contains at most ceil(P/2) disjoint runs.
 * Reserve the final native/compact array in the same cold block as page keys;
 * faults neither allocate nor relocate records. Zero-initialize the owner. */
static INLINE int xe_resolve_readback_init(xe_resolve_readback *s, size_t native_stride) {
  size_t owner_bytes = sizeof(retro_atomic_64_t) * XE_MEMEXPORT_PAGE_COUNT;
  size_t stride = native_stride ? native_stride : sizeof(xe_memexport_run);
  uint32_t i;
  void *block;
  if (s->owners || (native_stride && native_stride != XE_RESOLVE_NATIVE_STRIDE)) return 0;
  block = XE_RESOLVE_ALLOC(owner_bytes + stride * XE_RESOLVE_MAX_RUNS);
  if (!block) return 0;
  s->owners = (retro_atomic_64_t *)block;
  s->records = (unsigned char *)block + owner_bytes; s->stride = (uint32_t)stride;
  for (i = 0; i < XE_MEMEXPORT_PAGE_COUNT; ++i) retro_atomic_store_relaxed_64(s->owners + i, 0);
  return 1;
}
static INLINE void xe_resolve_readback_destroy(xe_resolve_readback *s) {
  XE_RESOLVE_FREE((void *)s->owners); s->owners = NULL; s->records = NULL; s->stride = 0;
}
/* Clamp before addition, including UINT32_MAX-sized host access spans. */
static INLINE int xe_resolve_page_window(uint32_t base, uint32_t size,
    uint32_t *first, uint32_t *last) {
  if (!size || base >= XE_MEMEXPORT_BUFFER_SIZE) return 0;
  if (size > XE_MEMEXPORT_BUFFER_SIZE - base) size = XE_MEMEXPORT_BUFFER_SIZE - base;
  *first = base >> 12; *last = (base + size - 1) >> 12;
  return 1;
}
/* Caller proves page is in the physical aperture. Relaxed keys do not publish
 * ResolveOwner objects or the contents of native GPU resources. */
static INLINE uint64_t xe_resolve_owner_load(const xe_resolve_readback *s, uint32_t page) {
  return (uint64_t)retro_atomic_load_relaxed_64(s->owners + page);
}
static INLINE void xe_resolve_owner_set(xe_resolve_readback *s,
    uint32_t base, uint32_t size, uint64_t key) {
  uint32_t first, last, page;
  if (!s->owners || !xe_resolve_page_window(base, size, &first, &last)) return;
  for (page = first; page <= last; ++page) retro_atomic_store_relaxed_64(s->owners + page, (int64_t)key);
}
static INLINE uint32_t xe_resolve_emit(unsigned char *record, uint32_t stride,
    uint32_t first, uint32_t end, uint32_t base, uint32_t limit,
    int packed, uint32_t packed_offset) {
  uint32_t at = first << 12, finish = end << 12, size;
  if (at < base) at = base;
  if (finish > limit) finish = limit;
  size = finish - at;
  if (stride == sizeof(xe_memexport_run)) {
    memcpy(record, &at, sizeof(at)); memcpy(record + 4, &size, sizeof(size));
  } else {
    uint64_t src = at, dst = packed ? packed_offset : at, length = size;
    memcpy(record, &src, sizeof(src)); memcpy(record + 8, &dst, sizeof(dst));
    memcpy(record + 16, &length, sizeof(length));
  }
  return size;
}
/* One load per page, one final write per run. Other threads may change atomic
 * keys while capturing; recording/delivery subsequently read only records.
 * Edge clipping is applied only where selected runs reach owner edge pages. */
static INLINE int xe_resolve_snapshot_make(xe_resolve_readback *s,
    uint64_t key, uint32_t base, uint32_t size, int clip_first, int clip_last,
    int packed, xe_resolve_copy_plan *plan) {
  uint32_t first, last, page, begin, end, start = UINT32_MAX, run_end = 0;
  uint32_t count = 0, bytes = 0, stride = s->stride;
  const retro_atomic_64_t *owners = s->owners;
  unsigned char *record = (unsigned char *)s->records;
  plan->records = s->records; plan->stride = s->stride;
  plan->count = plan->bytes = 0;
  if (!s->owners || !key || !xe_resolve_page_window(base, size, &first, &last)) return 0;
  if (size > XE_MEMEXPORT_BUFFER_SIZE - base) size = XE_MEMEXPORT_BUFFER_SIZE - base;
  begin = clip_first ? base : first << 12;
  end = clip_last ? base + size : (last + 1) << 12;
  for (page = first; page <= last; ++page) {
    if ((uint64_t)retro_atomic_load_relaxed_64(owners + page) != key) continue;
    if (start == UINT32_MAX) start = page;
    else if (page != run_end) {
      bytes += xe_resolve_emit(record, stride, start, run_end, begin, end, packed, bytes);
      record += stride; ++count; start = page;
    }
    run_end = page + 1;
  }
  if (start != UINT32_MAX) {
    bytes += xe_resolve_emit(record, stride, start, run_end, begin, end, packed, bytes);
    ++count;
  }
  plan->count = count; plan->bytes = bytes;
  return plan->count != 0;
}
static INLINE xe_resolve_copy_cursor xe_resolve_cursor_make(const xe_resolve_copy_plan *plan) {
  xe_resolve_copy_cursor cursor;
  cursor.records = (const unsigned char *)plan->records;
  cursor.remaining = plan->count; cursor.stride = plan->stride;
  return cursor;
}
static INLINE int xe_resolve_next(xe_resolve_copy_cursor *cursor, xe_memexport_run *run) {
  if (!cursor->remaining) return 0;
  if (cursor->stride == sizeof(xe_memexport_run)) memcpy(run, cursor->records, sizeof(*run));
  else {
    uint64_t base, size;
    memcpy(&base, cursor->records, sizeof(base)); memcpy(&size, cursor->records + 16, sizeof(size));
    run->base = (uint32_t)base; run->size = (uint32_t)size;
  }
  --cursor->remaining; cursor->records += cursor->stride;
  return 1;
}
#ifdef __cplusplus
struct xe_resolve_readback_owner : xe_resolve_readback {
  xe_resolve_readback_owner() : xe_resolve_readback{} {}
  xe_resolve_readback_owner(const xe_resolve_readback_owner&) = delete;
  xe_resolve_readback_owner& operator=(const xe_resolve_readback_owner&) = delete;
  ~xe_resolve_readback_owner() { xe_resolve_readback_destroy(this); }
};
#endif
#endif
