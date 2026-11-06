/* Shared-memory validity hints. Writers remain serialized by the owner's
 * critical region; readers may load words without that lock. These bits do
 * not publish completed GPU data or replace native submission/fence ordering. */
#ifndef XE_CORE_UPLOAD_SCAN_H
#define XE_CORE_UPLOAD_SCAN_H
#include <stddef.h>
#include <stdlib.h>
#include <retro_atomic.h>
#include <compat/intrinsics.h>
#include "core_upload_ranges.h"
#ifndef RETRO_ATOMIC_HAS_64
#error Shared-memory validity requires libretro-common 64-bit atomics
#endif
#ifndef XE_UPLOAD_SCAN_ALLOC
#define XE_UPLOAD_SCAN_ALLOC malloc
#define XE_UPLOAD_SCAN_FREE free
#endif
typedef retro_atomic_64_t xe_page_valid_word;
typedef char xe_page_valid_word_size[sizeof(xe_page_valid_word) == sizeof(uint64_t) ? 1 : -1];
static INLINE uint64_t xe_page_valid_load(const xe_page_valid_word *word) {
  return (uint64_t)retro_atomic_load_relaxed_64(word);
}
static INLINE void xe_page_valid_store(xe_page_valid_word *word, uint64_t value) {
  retro_atomic_store_relaxed_64(word, (int64_t)value);
}
/* The caller serializes writers. Avoid locked RMWs while keeping every access
 * atomic for lock-free readers, including reset/trace/cache-refresh paths. */
static INLINE void xe_page_valid_or(xe_page_valid_word *word, uint64_t bits) {
  xe_page_valid_store(word, xe_page_valid_load(word) | bits);
}
static INLINE void xe_page_valid_and(xe_page_valid_word *word, uint64_t bits) {
  xe_page_valid_store(word, xe_page_valid_load(word) & bits);
}
static INLINE void xe_page_valid_clear(xe_page_valid_word *words, uint32_t count) {
  uint32_t i;
  if (!words) return;
  for (i = 0; i < count; ++i) xe_page_valid_store(words + i, 0);
}
/* Source words are protected by the caller's existing critical region.
 * Clamp the final partial chunk instead of assuming 64 remaining words. */
static INLINE void xe_page_valid_copy(xe_page_valid_word *words,
    const uint64_t *source, uint32_t count, uint32_t first, uint32_t length) {
  uint32_t i;
  if (!words || !source || first >= count) return;
  if (length > count - first) length = count - first;
  for (i = first; i < first + length; ++i) xe_page_valid_store(words + i, source[i]);
}
/* Internal fast path after the caller validates byte bounds and nonzero length.
 * Requires nonnull words and first <= last within the allocated page aperture. */
static INLINE int xe_upload_all_valid_unchecked(const xe_page_valid_word *words,
    uint32_t first, uint32_t last) {
  uint32_t i, begin = first >> 6, end = last >> 6;
  uint64_t low = UINT64_MAX << (first & 63);
  uint64_t high = UINT64_MAX >> (63 - (last & 63));
  if (begin == end) {
    uint64_t mask = low & high;
    return (xe_page_valid_load(words + begin) & mask) == mask;
  }
  if ((xe_page_valid_load(words + begin) & low) != low) return 0;
  for (i = begin + 1; i < end; ++i)
    if (xe_page_valid_load(words + i) != UINT64_MAX) return 0;
  return (xe_page_valid_load(words + end) & high) == high;
}
static INLINE int xe_upload_all_valid(const xe_page_valid_word *words,
    uint32_t pages, uint32_t first, uint32_t last) {
  if (!words || first > last || last >= pages) return 0;
  return xe_upload_all_valid_unchecked(words, first, last);
}
typedef struct xe_upload_scan {
  xe_upload_range *data;
  uint32_t pages, capacity, count;
} xe_upload_scan;
/* P pages contain at most ceil(P/2) disjoint nonempty invalid runs. Reserve
 * that hardware-derived bound once, without initializing unused range bytes. */
static INLINE int xe_upload_scan_init(xe_upload_scan *s, uint32_t pages) {
  uint32_t capacity;
  size_t bytes;
  xe_upload_range *data;
  if (s->data || !pages) return 0;
  capacity = (pages >> 1) + (pages & 1);
  bytes = (size_t)capacity * sizeof(*data);
  if (bytes / sizeof(*data) != capacity) return 0;
  data = (xe_upload_range *)XE_UPLOAD_SCAN_ALLOC(bytes);
  if (!data) return 0;
  s->data = data; s->pages = pages; s->capacity = capacity; s->count = 0;
  return 1;
}
/* Collection preflights the worst-case run count before the first store. */
static INLINE void xe_upload_scan_emit(xe_upload_range *data,
    uint32_t *count, uint32_t start, uint32_t end) {
  data[*count].first = start; data[*count].second = end - start;
  ++*count;
}
/* Caller holds the validity/watch critical region throughout collection.
 * Write directly into the final owner span, leaving validity words unchanged.
 * Worst-case capacity is checked before any output store. No per-run bounds
 * branch or intermediate count publication is needed; validity stays unchanged. */
static INLINE int xe_upload_scan_collect(xe_upload_scan *s,
    const xe_page_valid_word *words, uint32_t first, uint32_t last) {
  uint32_t i, count = 0, extent, start = UINT32_MAX;
  xe_upload_range *data = s->data;
  uint32_t word_first = first >> 6, word_last = last >> 6;
  uint64_t before = (UINT64_C(1) << (first & 63)) - 1;
  uint64_t after = ~(UINT64_MAX >> (63 - (last & 63)));
  s->count = 0;
  if (!s->data || !words || first > last || last >= s->pages) return 0;
  /* Any queried extent has at most ceil(extent/2) disjoint invalid runs. */
  extent = last - first + 1;
  if (s->capacity < (extent >> 1) + (extent & 1)) return 0;
  for (i = word_first; i <= word_last; ++i) {
    uint64_t valid = xe_page_valid_load(words + i);
    if (i == word_first) valid |= before;
    if (i == word_last) valid |= after;
    if (valid == UINT64_MAX) {
      if (start != UINT32_MAX) {
        xe_upload_scan_emit(data, &count, start, i << 6);
        start = UINT32_MAX;
      }
      continue;
    }
    if (!valid) {
      if (start == UINT32_MAX) start = i << 6;
      continue;
    }
    for (;;) {
      uint64_t ends;
      uint32_t bit;
      if (start == UINT32_MAX) {
        if (valid == UINT64_MAX) break;
        start = (i << 6) + compat_ctz_u64(~valid);
      }
      ends = valid;
      if ((start >> 6) == i) ends &= UINT64_MAX << (start & 63);
      if (!ends) break;
      bit = compat_ctz_u64(ends);
      xe_upload_scan_emit(data, &count, start, (i << 6) + bit);
      valid |= (UINT64_C(1) << bit) - 1;
      start = UINT32_MAX;
    }
  }
  if (start != UINT32_MAX) xe_upload_scan_emit(data, &count, start, last + 1);
  s->count = count; return 1;
}
static INLINE void xe_upload_scan_destroy(xe_upload_scan *s) {
  XE_UPLOAD_SCAN_FREE(s->data); s->data = NULL; s->pages = s->capacity = s->count = 0;
}
#ifdef __cplusplus
struct xe_upload_scan_owner : xe_upload_scan {
  xe_upload_scan_owner() : xe_upload_scan{} {}
  xe_upload_scan_owner(const xe_upload_scan_owner&) = delete;
  xe_upload_scan_owner& operator=(const xe_upload_scan_owner&) = delete;
  ~xe_upload_scan_owner() { xe_upload_scan_destroy(this); }
};
#endif
#endif
