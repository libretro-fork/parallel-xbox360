/* Borrowed little-endian PE32 wire table; no typed/unaligned guest loads. */
#ifndef XE_CORE_PE_IMAGE_H
#define XE_CORE_PE_IMAGE_H
#include "core_pe_section.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <retro_inline.h>
#ifndef XE_PE_REALLOC
#define XE_PE_REALLOC realloc
#define XE_PE_FREE free
#endif
typedef struct xe_pe_view {
  const uint8_t *sections;
  size_t count;
  uint32_t base, resident_size;
} xe_pe_view;
typedef struct xe_pe_sections {
  xe_pe_section *data;
  size_t size, capacity;
} xe_pe_sections;
static INLINE unsigned xe_pe_u16(const uint8_t *p) {
  return (unsigned)p[0] | ((unsigned)p[1] << 8);
}
static INLINE uint32_t xe_pe_u32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
      ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
/* Match the existing key-retry probe exactly, with an explicit byte extent. */
static INLINE int xe_pe_probe(const uint8_t *p, size_t length) {
  return p && length >= 4 && xe_pe_u32(p) == 0x00905a4dU;
}
static INLINE int xe_pe_validate(const uint8_t *p, size_t length,
    uint32_t base, xe_pe_view *view) {
  size_t nt, table, count, i;
  uint32_t address, size, raw;
  const uint8_t *record;
  if (!p || !view || length < 64 || length > UINT32_MAX - base ||
      xe_pe_u16(p) != 0x5a4d) return 0;
  nt = xe_pe_u32(p + 60);
  if ((nt & 0x80000000U) || nt > length || length - nt < 248) return 0;
  if (xe_pe_u32(p + nt) != 0x4550 || xe_pe_u16(p + nt + 4) != 0x1f2 ||
      !(xe_pe_u16(p + nt + 22) & 0x100) || xe_pe_u16(p + nt + 20) != 224 ||
      xe_pe_u16(p + nt + 24) != 0x10b || xe_pe_u16(p + nt + 92) != 14) return 0;
  table = nt + 248; count = xe_pe_u16(p + nt + 6);
  if (count > (length - table) / 40) return 0;
  for (i = 0; i < count; ++i) {
    record = p + table + i * 40;
    size = xe_pe_u32(record + 8); address = xe_pe_u32(record + 12);
    raw = xe_pe_u32(record + 16);
    /* Discardable PE tails may be omitted by XEX (SCIV .reloc). Keep strict
     * guest-address overflow checks; only discardable snapshots are clipped. */
    if (address > UINT32_MAX - base || size > UINT32_MAX - base - address ||
        raw > UINT32_MAX - base - address) return 0;
    if ((address > length || size > length - address || raw > length - address) &&
        !(xe_pe_u32(record + 36) & 0x02000000U)) return 0;
  }
  view->sections = p + table; view->count = count; view->base = base;
  view->resident_size = (uint32_t)length; return 1;
}
/* Call only with a validated immutable view disjoint from this owner. Retain
 * snapshots, since guest writes can change the wire table after loading. */
static INLINE int xe_pe_sections_read(xe_pe_sections *s, const xe_pe_view *view) {
  xe_pe_section *data;
  const uint8_t *record;
  size_t i, capacity;
  if (!s || !view || view->count > (size_t)-1 / sizeof(*data)) return 0;
  if (view->count > s->capacity) {
    capacity = s->capacity;
    if (capacity <= (size_t)-1 / (2 * sizeof(*data))) capacity *= 2;
    if (capacity < view->count) capacity = view->count;
    data = (xe_pe_section *)XE_PE_REALLOC(s->data, capacity * sizeof(*data));
    if (!data) return 0;
    s->data = data; s->capacity = capacity;
  }
  for (i = 0; i < view->count; ++i) {
    record = view->sections + i * 40; data = s->data + i;
    memcpy(data->name, record, 8); data->name[8] = 0;
    data->size = xe_pe_u32(record + 8); data->address = view->base + xe_pe_u32(record + 12);
    data->raw_size = xe_pe_u32(record + 16); data->raw_address = xe_pe_u32(record + 20);
    data->flags = xe_pe_u32(record + 36);
    if (data->flags & 0x02000000U) {
      uint32_t rva = xe_pe_u32(record + 12), available;
      if (rva > view->resident_size) rva = view->resident_size;
      available = view->resident_size - rva;
      data->address = view->base + rva;
      if (data->size > available) data->size = available;
      if (data->raw_size > available) data->raw_size = available;
    }
  }
  s->size = view->count; return 1;
}
static INLINE void xe_pe_sections_free(xe_pe_sections *s) {
  XE_PE_FREE(s->data); s->data = NULL; s->size = s->capacity = 0;
}
#endif
