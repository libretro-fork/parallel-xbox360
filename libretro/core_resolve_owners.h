/* Bounded resolve-owner registry. The caller serializes all access, and must
 * finish borrowed row use before erase/reset. Hash deletion moves index links,
 * never live rows. No heap allocation or callback is used here. */
#ifndef XE_CORE_RESOLVE_OWNERS_H
#define XE_CORE_RESOLVE_OWNERS_H
#include <stdint.h>
#include <string.h>
#include <compat/intrinsics.h>
#include "core_resolve_readback.h"
#define XE_RESOLVE_OWNER_CAPACITY 4096
#define XE_RESOLVE_OWNER_BUCKETS 8192
#define XE_RESOLVE_OWNER_NONE UINT16_MAX
typedef struct xe_resolve_owner_state {
  uint32_t address, length;
  uint64_t submission;
  uint32_t frame;
  uint8_t copyable, in_guest_ram, rearm_pending, clip_first_page, clip_last_page;
} xe_resolve_owner_state;
typedef struct xe_resolve_owners {
  xe_resolve_owner_state rows[XE_RESOLVE_OWNER_CAPACITY];
  uint64_t keys[XE_RESOLVE_OWNER_CAPACITY];
  uint64_t occupied[XE_RESOLVE_OWNER_CAPACITY / 64];
  uint16_t buckets[XE_RESOLVE_OWNER_BUCKETS];
  uint16_t location[XE_RESOLVE_OWNER_CAPACITY], next_free[XE_RESOLVE_OWNER_CAPACITY];
  uint32_t ages[XE_RESOLVE_OWNER_CAPACITY];
  uint32_t count;
  uint16_t free_head;
} xe_resolve_owners;
static INLINE void xe_resolve_owners_reset(xe_resolve_owners *s) {
  uint32_t i;
  memset(s->buckets, 0xff, sizeof(s->buckets));
  memset(s->occupied, 0, sizeof(s->occupied));
  for (i = 0; i + 1 < XE_RESOLVE_OWNER_CAPACITY; ++i) s->next_free[i] = (uint16_t)(i + 1);
  s->next_free[XE_RESOLVE_OWNER_CAPACITY - 1] = XE_RESOLVE_OWNER_NONE;
  s->free_head = 0; s->count = 0;
}
static INLINE uint32_t xe_resolve_owner_bucket(uint64_t key) {
  key ^= key >> 32; key *= UINT64_C(0xd6e8feb86659fd93); key ^= key >> 32;
  return (uint32_t)key & (XE_RESOLVE_OWNER_BUCKETS - 1);
}
static INLINE uint16_t xe_resolve_owners_find_id(const xe_resolve_owners *s, uint64_t key) {
  uint32_t bucket = xe_resolve_owner_bucket(key);
  uint16_t id;
  while ((id = s->buckets[bucket]) != XE_RESOLVE_OWNER_NONE) {
    if (s->keys[id] == key) return id;
    bucket = (bucket + 1) & (XE_RESOLVE_OWNER_BUCKETS - 1);
  }
  return XE_RESOLVE_OWNER_NONE;
}
static INLINE xe_resolve_owner_state *xe_resolve_owners_find(xe_resolve_owners *s, uint64_t key) {
  uint16_t id = xe_resolve_owners_find_id(s, key);
  return id == XE_RESOLVE_OWNER_NONE ? NULL : s->rows + id;
}
/* Existing rows retain their address and contents. New rows start zeroed. */
static INLINE xe_resolve_owner_state *xe_resolve_owners_get(xe_resolve_owners *s, uint64_t key) {
  uint32_t bucket = xe_resolve_owner_bucket(key);
  uint16_t id;
  while ((id = s->buckets[bucket]) != XE_RESOLVE_OWNER_NONE) {
    if (s->keys[id] == key) return s->rows + id;
    bucket = (bucket + 1) & (XE_RESOLVE_OWNER_BUCKETS - 1);
  }
  id = s->free_head;
  if (id == XE_RESOLVE_OWNER_NONE) return NULL;
  s->free_head = s->next_free[id];
  s->buckets[bucket] = id; s->location[id] = (uint16_t)bucket; s->keys[id] = key;
  s->occupied[id >> 6] |= UINT64_C(1) << (id & 63); ++s->count;
  memset(s->rows + id, 0, sizeof(s->rows[id]));
  return s->rows + id;
}
/* Backshift keeps searches bounded by an empty bucket without tombstones.
 * A half-full index always has empties, even with adversarial collisions. */
static INLINE void xe_resolve_owners_erase(xe_resolve_owners *s, uint32_t id) {
  uint32_t hole, scan;
  const uint32_t mask = XE_RESOLVE_OWNER_BUCKETS - 1;
  if (id >= XE_RESOLVE_OWNER_CAPACITY || !(s->occupied[id >> 6] & (UINT64_C(1) << (id & 63)))) return;
  hole = s->location[id]; scan = (hole + 1) & mask;
  while (s->buckets[scan] != XE_RESOLVE_OWNER_NONE) {
    uint16_t moving = s->buckets[scan];
    uint32_t home = xe_resolve_owner_bucket(s->keys[moving]);
    if (((scan - home) & mask) >= ((scan - hole) & mask)) {
      s->buckets[hole] = moving; s->location[moving] = (uint16_t)hole; hole = scan;
    }
    scan = (scan + 1) & mask;
  }
  s->buckets[hole] = XE_RESOLVE_OWNER_NONE;
  s->occupied[id >> 6] &= ~(UINT64_C(1) << (id & 63));
  s->next_free[id] = s->free_head; s->free_head = (uint16_t)id; --s->count;
}
/* Borrow live row IDs in ascending storage order. Erase may change index links
 * but does not move any other row or disturb this iteration order. */
static INLINE uint32_t xe_resolve_owners_next(const xe_resolve_owners *s, uint32_t first) {
  uint32_t word;
  uint64_t bits;
  if (first >= XE_RESOLVE_OWNER_CAPACITY) return XE_RESOLVE_OWNER_CAPACITY;
  word = first >> 6; bits = s->occupied[word] & (UINT64_MAX << (first & 63));
  while (!bits) {
    if (++word == XE_RESOLVE_OWNER_CAPACITY / 64) return XE_RESOLVE_OWNER_CAPACITY;
    bits = s->occupied[word];
  }
  return (word << 6) + compat_ctz_u64(bits);
}
static INLINE void xe_resolve_age_swap(uint32_t *a, uint32_t *b) {
  uint32_t t = *a; *a = *b; *b = t;
}
static INLINE void xe_resolve_age_sift(uint32_t *a, uint32_t size, uint32_t root) {
  while (root < size / 2) {
    uint32_t child = root * 2 + 1;
    if (child + 1 < size && a[child] < a[child + 1]) ++child;
    if (a[root] >= a[child]) return;
    xe_resolve_age_swap(a + root, a + child); root = child;
  }
}
static INLINE void xe_resolve_age_heapsort(uint32_t *a, uint32_t size) {
  uint32_t i;
  for (i = size / 2; i; --i) xe_resolve_age_sift(a, size, i - 1);
  for (i = size; i > 1; --i) { xe_resolve_age_swap(a, a + i - 1); xe_resolve_age_sift(a, i - 1, 0); }
}
/* In-place unsigned selection. Three-way partition handles ties; a bounded
 * depth falls back to allocation-free heapsort for adversarial pivots. */
static INLINE uint32_t xe_resolve_age_select(uint32_t *a, uint32_t count, uint32_t rank) {
  uint32_t left = 0, right = count, depth = 0, n = count;
  if (!count || count > XE_RESOLVE_OWNER_CAPACITY || rank >= count) return 0;
  while (n >>= 1) depth += 2;
  while (right - left > 16) {
    uint32_t x = a[left], y = a[left + (right - left) / 2], z = a[right - 1];
    uint32_t pivot, low = left, scan = left, high = right;
    if (!depth--) { xe_resolve_age_heapsort(a + left, right - left); return a[rank]; }
    if (x > y) { uint32_t t = x; x = y; y = t; }
    if (y > z) y = z;
    pivot = x > y ? x : y;
    while (scan < high) {
      if (a[scan] < pivot) { xe_resolve_age_swap(a + scan, a + low); ++low; ++scan; }
      else if (a[scan] > pivot) { --high; xe_resolve_age_swap(a + scan, a + high); }
      else ++scan;
    }
    if (rank < low) right = low;
    else if (rank >= high) left = high;
    else return pivot;
  }
  for (n = left + 1; n < right; ++n) {
    uint32_t value = a[n], at = n;
    while (at > left && a[at - 1] > value) { a[at] = a[at - 1]; --at; }
    a[at] = value;
  }
  return a[rank];
}
/* Pruning drops copied/unowned edges, then (only if still full) every row at or
 * above the unsigned 75th percentile age. At least a quarter becomes reusable.
 * Snapshot ages into retained scratch because selection reorders its input. */
static INLINE void xe_resolve_owners_prune(xe_resolve_owners *s,
    const xe_resolve_readback *pages, uint32_t frame) {
  uint32_t id, count = 0, min_age;
  for (id = xe_resolve_owners_next(s, 0); id < XE_RESOLVE_OWNER_CAPACITY; id = xe_resolve_owners_next(s, id + 1)) {
    const xe_resolve_owner_state *row = s->rows + id;
    uint32_t first, last;
    if (!xe_resolve_page_window(row->address, row->length, &first, &last) ||
        row->in_guest_ram || (xe_resolve_owner_load(pages, first) != s->keys[id] &&
                             xe_resolve_owner_load(pages, last) != s->keys[id])) xe_resolve_owners_erase(s, id);
  }
  if (s->count < XE_RESOLVE_OWNER_CAPACITY) return;
  for (id = xe_resolve_owners_next(s, 0); id < XE_RESOLVE_OWNER_CAPACITY; id = xe_resolve_owners_next(s, id + 1))
    s->ages[count++] = frame - s->rows[id].frame;
  min_age = xe_resolve_age_select(s->ages, count, count * 3 / 4);
  for (id = xe_resolve_owners_next(s, 0); id < XE_RESOLVE_OWNER_CAPACITY; id = xe_resolve_owners_next(s, id + 1))
    if (frame - s->rows[id].frame >= min_age) xe_resolve_owners_erase(s, id);
}
static INLINE void xe_resolve_owners_clear_rearm(xe_resolve_owners *s) {
  uint32_t id;
  for (id = xe_resolve_owners_next(s, 0); id < XE_RESOLVE_OWNER_CAPACITY; id = xe_resolve_owners_next(s, id + 1)) s->rows[id].rearm_pending = 0;
}
#ifdef __cplusplus
struct xe_resolve_owners_storage : xe_resolve_owners {
  xe_resolve_owners_storage() { xe_resolve_owners_reset(this); }
  xe_resolve_owners_storage(const xe_resolve_owners_storage&) = delete;
  xe_resolve_owners_storage& operator=(const xe_resolve_owners_storage&) = delete;
};
#endif
#endif
