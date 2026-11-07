/* GPU-thread-only recent resolve reads. Exact keys and unsigned frame ages;
 * this history deliberately outlives individual resolve-owner rows. */
#ifndef XE_CORE_RESOLVE_RECENT_H
#define XE_CORE_RESOLVE_RECENT_H
#include <stdint.h>
#include <string.h>
#include <retro_inline.h>
#include <compat/intrinsics.h>
#define XE_RESOLVE_RECENT_CAPACITY 4096u
#define XE_RESOLVE_RECENT_BUCKETS 8192u
/* About a second, chosen, not measured; preserved from the original policy. */
#define XE_RESOLVE_RECENT_FRAMES 60u

typedef struct xe_resolve_recent {
  uint64_t keys[XE_RESOLVE_RECENT_BUCKETS];
  uint32_t frames[XE_RESOLVE_RECENT_BUCKETS];
  uint64_t occupied[XE_RESOLVE_RECENT_BUCKETS / 64];
  uint32_t count;
} xe_resolve_recent;
static INLINE void xe_resolve_recent_reset(xe_resolve_recent *s) {
  memset(s->occupied, 0, sizeof(s->occupied));
  s->count = 0;
}
static INLINE uint32_t xe_resolve_recent_home(uint64_t key) {
  key ^= key >> 32;
  key *= UINT64_C(0xd6e8feb86659fd93);
  key ^= key >> 32;
  return (uint32_t)key & (XE_RESOLVE_RECENT_BUCKETS - 1);
}
static INLINE int xe_resolve_recent_live(const xe_resolve_recent *s, uint32_t at) {
  return (s->occupied[at >> 6] & (UINT64_C(1) << (at & 63))) != 0;
}
/* At most half full: probing always reaches an empty bucket. Dead payloads
 * need not be initialized and are never read. Zero and all-bit keys are valid. */
static INLINE uint32_t xe_resolve_recent_slot(const xe_resolve_recent *s, uint64_t key) {
  uint32_t at = xe_resolve_recent_home(key);
  while (xe_resolve_recent_live(s, at) && s->keys[at] != key)
    at = (at + 1) & (XE_RESOLVE_RECENT_BUCKETS - 1);
  return at;
}
/* No row views escape this GPU-thread history. Only the index cluster moves;
 * owner payloads and fault-thread leases belong to a separate registry. */
static INLINE void xe_resolve_recent_erase(xe_resolve_recent *s, uint32_t hole) {
  uint32_t scan = (hole + 1) & (XE_RESOLVE_RECENT_BUCKETS - 1);
  const uint32_t mask = XE_RESOLVE_RECENT_BUCKETS - 1;
  while (xe_resolve_recent_live(s, scan)) {
    uint32_t home = xe_resolve_recent_home(s->keys[scan]);
    if (((scan - home) & mask) >= ((scan - hole) & mask)) {
      s->keys[hole] = s->keys[scan];
      s->frames[hole] = s->frames[scan];
      hole = scan;
    }
    scan = (scan + 1) & mask;
  }
  s->occupied[hole >> 6] &= ~(UINT64_C(1) << (hole & 63));
  --s->count;
}
/* Skip empty buckets by occupancy word without collecting a temporary list. */
static INLINE uint32_t xe_resolve_recent_next(const xe_resolve_recent *s, uint32_t first) {
  uint32_t word;
  uint64_t bits;
  if (first >= XE_RESOLVE_RECENT_BUCKETS) return XE_RESOLVE_RECENT_BUCKETS;
  word = first >> 6;
  bits = s->occupied[word] & (UINT64_MAX << (first & 63));
  while (!bits) {
    if (++word == XE_RESOLVE_RECENT_BUCKETS / 64) return XE_RESOLVE_RECENT_BUCKETS;
    bits = s->occupied[word];
  }
  return (word << 6) + compat_ctz_u64(bits);
}
/* Called only when full, before even an existing-key update, matching the old
 * history policy. Recheck an erased bucket because backshift can fill it. */
static INLINE void xe_resolve_recent_prune(xe_resolve_recent *s, uint32_t frame) {
  uint32_t at = 0;
  while ((at = xe_resolve_recent_next(s, at)) < XE_RESOLVE_RECENT_BUCKETS) {
    if (frame - s->frames[at] > XE_RESOLVE_RECENT_FRAMES)
      xe_resolve_recent_erase(s, at);
    else ++at;
  }
  if (s->count >= XE_RESOLVE_RECENT_CAPACITY) xe_resolve_recent_reset(s);
}
/* Queries never mutate history. A read remembers its current frame; expiry
 * pruning occurs only at the original cap. No host clock or pacing is added. */
static INLINE int xe_resolve_recent_read(xe_resolve_recent *s,
    uint64_t key, uint32_t frame, int read_before) {
  uint32_t at;
  if (read_before && s->count >= XE_RESOLVE_RECENT_CAPACITY)
    xe_resolve_recent_prune(s, frame);
  at = xe_resolve_recent_slot(s, key);
  if (!read_before)
    return xe_resolve_recent_live(s, at) && frame - s->frames[at] <= XE_RESOLVE_RECENT_FRAMES;
  if (!xe_resolve_recent_live(s, at)) {
    s->occupied[at >> 6] |= UINT64_C(1) << (at & 63);
    s->keys[at] = key;
    ++s->count;
  }
  s->frames[at] = frame;
  return 1;
}
#ifdef __cplusplus
struct xe_resolve_recent_storage : xe_resolve_recent {
  xe_resolve_recent_storage() { xe_resolve_recent_reset(this); }
  xe_resolve_recent_storage(const xe_resolve_recent_storage&) = delete;
  xe_resolve_recent_storage& operator=(const xe_resolve_recent_storage&) = delete;
};
#endif
#endif
