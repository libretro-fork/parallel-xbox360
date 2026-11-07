/* GPU-owner-only bounded pipeline handles. No nodes, rehash, locks or growth.
 * Eleven key bits cover the current EDRAM dump/direct-resolve permutations.
 * uint64 preserves Vulkan non-dispatchable handles even on 32-bit hosts. */
#ifndef XE_CORE_RESOLVE_PIPELINE_CACHE_H
#define XE_CORE_RESOLVE_PIPELINE_CACHE_H
#include <stdint.h>
#include <string.h>
#include "../third_party/libretro-common/include/retro_inline.h"
#define XE_RESOLVE_PIPELINE_CAPACITY 2048u
typedef struct xe_resolve_pipeline_cache {
  uint64_t handles[XE_RESOLVE_PIPELINE_CAPACITY];
  uint32_t seen[XE_RESOLVE_PIPELINE_CAPACITY / 32];
} xe_resolve_pipeline_cache;
static INLINE int xe_resolve_pipeline_get(const xe_resolve_pipeline_cache *cache,
    uint32_t key, uint64_t *handle) {
  if (key >= XE_RESOLVE_PIPELINE_CAPACITY || !handle ||
      !(cache->seen[key >> 5] & (UINT32_C(1) << (key & 31)))) return 0;
  *handle = cache->handles[key]; return 1;
}
/* A cached zero is a failed creation, not an empty slot. Never overwrite an
 * owner: a rejected publication leaves the new handle with its caller. */
static INLINE int xe_resolve_pipeline_put(xe_resolve_pipeline_cache *cache,
    uint32_t key, uint64_t handle) {
  uint32_t bit;
  if (key >= XE_RESOLVE_PIPELINE_CAPACITY) return 0;
  bit = UINT32_C(1) << (key & 31);
  if (cache->seen[key >> 5] & bit) return 0;
  cache->handles[key] = handle; cache->seen[key >> 5] |= bit; return 1;
}
/* Borrow values in key order for native destruction. No handle-array copy. */
static INLINE int xe_resolve_pipeline_next(const xe_resolve_pipeline_cache *cache,
    uint32_t *cursor, uint64_t *handle) {
  if (!cursor || !handle) return 0;
  while (*cursor < XE_RESOLVE_PIPELINE_CAPACITY) {
    uint32_t key = (*cursor)++;
    if (xe_resolve_pipeline_get(cache,key,handle)) return 1;
  }
  return 0;
}
/* Only after native destruction and the existing shutdown joins. Stale payload
 * need not be zero-filled: lookups never read a slot without its seen bit. */
static INLINE void xe_resolve_pipeline_reset(xe_resolve_pipeline_cache *cache) {
  memset(cache->seen,0,sizeof(cache->seen));
}
#endif
