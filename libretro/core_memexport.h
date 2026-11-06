/* GPU-command-thread memexport ownership. Borrowed plans require unchanged
 * words until recording finishes. No allocation, locks or CPU/GPU waits here. */
#ifndef XE_CORE_MEMEXPORT_H
#define XE_CORE_MEMEXPORT_H
#include <stdint.h>
#include <string.h>
#include <retro_inline.h>
#include <compat/intrinsics.h>
#define XE_MEMEXPORT_BUFFER_SIZE UINT32_C(0x20000000)
#define XE_MEMEXPORT_PAGE_COUNT (XE_MEMEXPORT_BUFFER_SIZE >> 12)
typedef struct xe_memexport_map {
  uint64_t words[XE_MEMEXPORT_PAGE_COUNT / 64];
  uint32_t first, last;
  int any;
} xe_memexport_map;
typedef struct xe_memexport_run { uint32_t base, size; } xe_memexport_run;
typedef struct xe_memexport_cursor {
  const uint64_t *words;
  uint32_t page, last, base, end;
  int whole;
  uint64_t pending;
  uint32_t word;
  int loaded;
} xe_memexport_cursor;
typedef struct xe_memexport_plan {
  xe_memexport_cursor cursor;
  uint32_t count, base, size;
} xe_memexport_plan;
static INLINE uint64_t xe_memexport_mask(uint32_t word, uint32_t first, uint32_t last) {
  uint64_t mask = UINT64_MAX;
  if (word == (first >> 6)) mask &= UINT64_MAX << (first & 63);
  if (word == (last >> 6)) mask &= UINT64_MAX >> (63 - (last & 63));
  return mask;
}
static INLINE int xe_memexport_mark(xe_memexport_map *s, uint32_t base, uint32_t size) {
  uint32_t first, last, i;
  if (!size || base >= XE_MEMEXPORT_BUFFER_SIZE) return 0;
  if (size > XE_MEMEXPORT_BUFFER_SIZE - base) size = XE_MEMEXPORT_BUFFER_SIZE - base;
  first = base >> 12; last = (base + size - 1) >> 12;
  for (i = first >> 6; i <= (last >> 6); ++i) s->words[i] |= xe_memexport_mask(i, first, last);
  if (!s->any || first < s->first) s->first = first;
  if (!s->any || last > s->last) s->last = last;
  s->any = 1; return 1;
}
/* Clear only complete pages, keeping neighbouring bytes authoritative. Bounds
 * deliberately remain conservative after clears, as in the original tracker. */
static INLINE void xe_memexport_clear(xe_memexport_map *s, uint32_t base, uint32_t size) {
  uint32_t first, end, i;
  if (!s->any || !size || base >= XE_MEMEXPORT_BUFFER_SIZE) return;
  if (size > XE_MEMEXPORT_BUFFER_SIZE - base) size = XE_MEMEXPORT_BUFFER_SIZE - base;
  first = (base + 4095) >> 12; end = (base + size) >> 12;
  if (first >= end) return;
  for (i = first >> 6; i <= ((end - 1) >> 6); ++i) s->words[i] &= ~xe_memexport_mask(i, first, end - 1);
}
static INLINE void xe_memexport_reset(xe_memexport_map *s) {
  memset(s->words, 0, sizeof(s->words)); s->first = XE_MEMEXPORT_PAGE_COUNT; s->last = 0; s->any = 0;
}
static INLINE int xe_memexport_window(const xe_memexport_map *s,
    uint32_t base, uint32_t size, xe_memexport_cursor *cursor) {
  uint32_t first, last;
  if (!s->any || !size || base >= XE_MEMEXPORT_BUFFER_SIZE) return 0;
  if (size > XE_MEMEXPORT_BUFFER_SIZE - base) size = XE_MEMEXPORT_BUFFER_SIZE - base;
  first = base >> 12; last = (base + size - 1) >> 12;
  if (first < s->first) first = s->first;
  if (last > s->last) last = s->last;
  if (first > last) return 0;
  cursor->words = s->words; cursor->page = first; cursor->last = last;
  cursor->base = base; cursor->end = base + size; cursor->whole = 0;
  cursor->pending = 0; cursor->word = 0; cursor->loaded = 0; return 1;
}
static INLINE int xe_memexport_cursor_any(const xe_memexport_cursor *cursor) {
  uint32_t i;
  for (i = cursor->page >> 6; i <= (cursor->last >> 6); ++i)
    if (cursor->words[i] & xe_memexport_mask(i, cursor->page, cursor->last)) return 1;
  return 0;
}
static INLINE int xe_memexport_has(const xe_memexport_map *s, uint32_t base, uint32_t size) {
  xe_memexport_cursor cursor;
  return xe_memexport_window(s, base, size, &cursor) && xe_memexport_cursor_any(&cursor);
}
/* D3D12 records individual copies and needs no preliminary run-count pass. */
static INLINE int xe_memexport_cursor_make(const xe_memexport_map *s,
    uint32_t base, uint32_t size, int whole, xe_memexport_cursor *cursor) {
  if (!xe_memexport_window(s, base, size, cursor) || !xe_memexport_cursor_any(cursor)) return 0;
  cursor->whole = whole != 0; return 1;
}
static INLINE int xe_memexport_next(xe_memexport_cursor *cursor, xe_memexport_run *run) {
  uint32_t word, start, end, byte_end;
  uint64_t bits;
  if (cursor->whole) {
    run->base = cursor->base; run->size = cursor->end - cursor->base;
    cursor->whole = 0; cursor->page = cursor->last + 1; return 1;
  }
  while (cursor->page <= cursor->last) {
    word = cursor->page >> 6;
    if (!cursor->loaded || cursor->word != word) {
      cursor->pending = cursor->words[word] & xe_memexport_mask(word, cursor->page, cursor->last);
      cursor->word = word; cursor->loaded = 1;
    }
    bits = cursor->pending;
    if (bits) goto found;
    cursor->page = (word + 1) << 6;
    cursor->loaded = 0;
  }
  return 0;
found:
  start = (word << 6) + compat_ctz_u64(bits);
  for (;;) {
    bits = ~cursor->pending;
    if (word == (start >> 6)) bits &= UINT64_MAX << (start & 63);
    if (bits) {
      end = (word << 6) + compat_ctz_u64(bits);
      /* Pending already masks the query tail, so this zero ends within the
       * current word and never needs another range clamp or word reload. */
      cursor->pending &= UINT64_MAX << (end & 63);
      break;
    }
    if (word == (cursor->last >> 6)) { end = cursor->last + 1; cursor->loaded = 0; break; }
    ++word;
    cursor->word = word;
    cursor->pending = cursor->words[word];
    if (word == (cursor->last >> 6)) cursor->pending &= UINT64_MAX >> (63 - (cursor->last & 63));
    cursor->loaded = 1;
  }
  cursor->page = end;
  run->base = start << 12;
  if (run->base < cursor->base) run->base = cursor->base;
  byte_end = end << 12;
  if (byte_end > cursor->end) byte_end = cursor->end;
  run->size = byte_end - run->base; return 1;
}
/* Portable word popcount; no host POPCNT feature requirement. */
static INLINE uint32_t xe_memexport_count_bits(uint64_t bits) {
  bits -= (bits >> 1) & UINT64_C(0x5555555555555555);
  bits = (bits & UINT64_C(0x3333333333333333)) + ((bits >> 2) & UINT64_C(0x3333333333333333));
  bits = (bits + (bits >> 4)) & UINT64_C(0x0f0f0f0f0f0f0f0f);
  return (uint32_t)((bits * UINT64_C(0x0101010101010101)) >> 56);
}
/* Analyze exact copy count/bounds once, then borrow the bitmap for emission.
 * Whole-range upload refresh needs only an overlap query and one scalar span. */
static INLINE int xe_memexport_plan_make(const xe_memexport_map *s,
    uint32_t base, uint32_t size, int whole, xe_memexport_plan *plan) {
  uint32_t i, end = 0;
  uint64_t carry = 0;
  plan->count = 0;
  if (!xe_memexport_window(s, base, size, &plan->cursor)) return 0;
  if (whole) {
    if (!xe_memexport_cursor_any(&plan->cursor)) return 0;
    plan->cursor.whole = 1; plan->count = 1; plan->base = base;
    plan->size = plan->cursor.end - base; return 1;
  }
  for (i = plan->cursor.page >> 6; i <= (plan->cursor.last >> 6); ++i) {
    uint64_t bits = s->words[i] & xe_memexport_mask(i, plan->cursor.page, plan->cursor.last);
    if (bits) {
      if (!plan->count) {
        plan->base = ((i << 6) + compat_ctz_u64(bits)) << 12;
        if (plan->base < base) plan->base = base;
      }
      plan->count += xe_memexport_count_bits((bits & ~(bits << 1)) & ~carry);
      end = ((i << 6) + 64 - compat_clz_u64(bits)) << 12;
    }
    carry = bits >> 63;
  }
  if (!plan->count) return 0;
  if (end > plan->cursor.end) end = plan->cursor.end;
  plan->size = end - plan->base; return 1;
}
#endif
