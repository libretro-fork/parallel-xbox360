/* Allocation-free ELF32 big-endian PPC loading. Input stays immutable/borrowed
 * through apply/rollback; guest memory/translation stay stable until return. */
#ifndef XE_CORE_ELF_H
#define XE_CORE_ELF_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <retro_inline.h>
typedef struct xe_elf_plan {
  const uint8_t *bytes;
  size_t size;
  uint32_t table, count, entry, page[2];
} xe_elf_plan;
typedef struct xe_elf_segment {
  uint32_t type, offset, address, file_size, memory_size, flags, alignment;
} xe_elf_segment;
typedef struct xe_elf_sink {
  void *opaque;
  /* Reserve free ranges or explicitly lent boot backing; never replace another
   * owner. Release retires only newly reserved storage, not lent backing. */
  int (*reserve)(void *, uint32_t base, uint32_t size);
  void (*release)(void *, uint32_t base, uint32_t size);
  uint8_t *(*translate)(void *, uint32_t address);
  void (*executable)(void *, uint32_t base, uint32_t end);
} xe_elf_sink;
typedef struct xe_elf_state { unsigned reserved; } xe_elf_state;
static INLINE uint32_t xe_elf_u32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
      ((uint32_t)p[2] << 8) | p[3];
}
static INLINE unsigned xe_elf_u16(const uint8_t *p) {
  return ((unsigned)p[0] << 8) | p[1];
}
/* Only use with a successfully validated, unmodified plan and index<count. */
static INLINE void xe_elf_segment_get(const xe_elf_plan *p, unsigned index,
    xe_elf_segment *s) {
  const uint8_t *b = p->bytes + p->table + (size_t)index * 32;
  s->type = xe_elf_u32(b); s->offset = xe_elf_u32(b + 4);
  s->address = xe_elf_u32(b + 8); s->file_size = xe_elf_u32(b + 16);
  s->memory_size = xe_elf_u32(b + 20); s->flags = xe_elf_u32(b + 24);
  s->alignment = xe_elf_u32(b + 28);
}
static INLINE int xe_elf_validate(xe_elf_plan *out, const void *bytes,
    size_t size, uint32_t page0, uint32_t page1) {
  xe_elf_plan p;
  xe_elf_segment s;
  const uint8_t *b = (const uint8_t *)bytes;
  uint32_t previous = 0;
  unsigned i, loads = 0, entry_loaded = 0;
  if (!out || !b || size < 52 || memcmp(b, "\177ELF", 4) ||
      b[4] != 1 || b[5] != 2 || b[6] != 1 || xe_elf_u16(b + 16) != 2 ||
      xe_elf_u16(b + 18) != 20 || xe_elf_u32(b + 20) != 1 ||
      xe_elf_u16(b + 40) < 52 || xe_elf_u16(b + 40) > size ||
      xe_elf_u16(b + 42) != 32 || !page0 || !page1 ||
      (page0 & (page0 - 1)) || (page1 & (page1 - 1)) ||
      page0 > 0x10000000U || page1 > 0x10000000U) return 0;
  p.bytes = b; p.size = size; p.table = xe_elf_u32(b + 28);
  p.count = xe_elf_u16(b + 44); p.entry = xe_elf_u32(b + 24);
  p.page[0] = page0; p.page[1] = page1;
  if (!p.count || !p.table || p.table > size ||
      p.count > (size - p.table) / 32 || !p.entry) return 0;
  for (i = 0; i < p.count; ++i) {
    xe_elf_segment_get(&p, i, &s);
    /* PT_DYNAMIC and other metadata do not independently load memory. */
    if (s.type != 1) continue;
    if (s.offset > size || s.file_size > size - s.offset ||
        s.file_size > s.memory_size || s.address < 0x80000000U ||
        s.address >= 0xa0000000U || s.memory_size > 0xa0000000U - s.address ||
        (loads && s.address < previous) ||
        (s.alignment > 1 && ((s.alignment & (s.alignment - 1)) ||
          ((s.address ^ s.offset) & (s.alignment - 1))))) return 0;
    previous = s.address; ++loads;
    if (p.entry >= s.address && p.entry - s.address < s.memory_size)
      entry_loaded = 1;
  }
  if (!loads || !entry_loaded) return 0;
  *out = p; return 1;
}
/* Ascending disjoint heap-page unions. Splits at the two guest heap boundary;
 * does not reserve padding up to the file's potentially much larger p_align.
 * Scan borrowed records rather than allocating a range/sort vector. */
typedef struct xe_elf_cursor {
  unsigned heap, index;
  uint32_t pending_base, pending_end;
} xe_elf_cursor;
/* Zero-initialize cursor before iteration. At most two table scans overall. */
static INLINE int xe_elf_range_next(const xe_elf_plan *p, xe_elf_cursor *cursor,
    uint32_t *base, uint32_t *size) {
  xe_elf_segment s;
  uint32_t low = cursor->pending_base, high = cursor->pending_end;
  uint32_t a, z, page, begin, end;
  cursor->pending_base = cursor->pending_end = 0;
  while (cursor->heap < 2) {
    begin = cursor->heap ? 0x90000000U : 0x80000000U;
    end = begin + 0x10000000U; page = p->page[cursor->heap];
    while (cursor->index < p->count) {
      xe_elf_segment_get(p, cursor->index++, &s);
      if (s.type != 1 || !s.memory_size) continue;
      a = s.address < begin ? begin : s.address;
      z = s.address + s.memory_size; if (z > end) z = end;
      if (a >= z) continue;
      a &= ~(page - 1); z = (z + page - 1) & ~(page - 1);
      if (!high) { low = a; high = z; }
      else if (a <= high) { if (z > high) high = z; }
      else {
        cursor->pending_base = a; cursor->pending_end = z;
        *base = low; *size = high - low; return 1;
      }
    }
    ++cursor->heap; cursor->index = 0;
    if (high) { *base = low; *size = high - low; return 1; }
  }
  return 0;
}
static INLINE void xe_elf_rollback(const xe_elf_plan *p, const xe_elf_sink *sink,
    xe_elf_state *state) {
  xe_elf_cursor cursor = {0, 0, 0, 0};
  uint32_t base, size;
  unsigned count = state->reserved;
  state->reserved = 0;
  while (count && xe_elf_range_next(p, &cursor, &base, &size)) {
    sink->release(sink->opaque, base, size); --count;
  }
}
/* All file validation and reservations precede writes. On callback failure no
 * later writes/notifications occur, and only newly reserved ranges retire.
 * A C++ scope guard may call rollback after an exception using this state. */
static INLINE int xe_elf_apply(const xe_elf_plan *p, const xe_elf_sink *sink,
    xe_elf_state *state) {
  xe_elf_cursor cursor = {0, 0, 0, 0};
  uint32_t base, size;
  unsigned i;
  xe_elf_segment s;
  uint8_t *destination;
  uintptr_t source_address, target_address;
  if (!p || !sink || !state || state->reserved || !sink->reserve ||
      !sink->release || !sink->translate || !sink->executable) return 0;
  while (xe_elf_range_next(p, &cursor, &base, &size)) {
    /* Check the whole commit range BEFORE reserving: heap commit/scribbling can
     * touch page padding as well as the segment. Translation is address-only. */
    destination = sink->translate(sink->opaque, base);
    if (!destination) goto fail;
    source_address = (uintptr_t)p->bytes; target_address = (uintptr_t)destination;
    if (target_address >= source_address ? target_address - source_address < p->size :
        source_address - target_address < size) goto fail;
    if (!sink->reserve(sink->opaque, base, size)) goto fail;
    ++state->reserved;
  }
  for (i = 0; i < p->count; ++i) {
    xe_elf_segment_get(p, i, &s);
    if (s.type != 1 || !s.memory_size) continue;
    destination = sink->translate(sink->opaque, s.address);
    if (!destination) goto fail;
  }
  for (i = 0; i < p->count; ++i) {
    xe_elf_segment_get(p, i, &s);
    if (s.type != 1 || !s.memory_size) continue;
    destination = sink->translate(sink->opaque, s.address);
    if (!destination) goto fail;
    if (s.file_size) memcpy(destination, p->bytes + s.offset, s.file_size);
    if (s.memory_size != s.file_size)
      memset(destination + s.file_size, 0, s.memory_size - s.file_size);
  }
  for (i = 0; i < p->count; ++i) {
    xe_elf_segment_get(p, i, &s);
    if (s.type == 1 && s.memory_size && (s.flags & 1))
      sink->executable(sink->opaque, s.address & ~3U,
          (s.address + s.memory_size + 3) & ~3U);
  }
  state->reserved = 0; return 1;
fail:
  xe_elf_rollback(p, sink, state); return 0;
}
#endif
