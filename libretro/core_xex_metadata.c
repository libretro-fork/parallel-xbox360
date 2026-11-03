#include "core_xex_metadata.h"
#include <string.h>

static uint32_t xex_be32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
      ((uint32_t)p[2] << 8) | p[3];
}

static const uint8_t *xex_get(const xe_xex_source *s, uint64_t offset,
    size_t size, uint8_t *scratch, uint64_t limit) {
  const uint8_t *p;
  if (offset > limit || size > limit - offset) return NULL;
  p = s->view ? s->view(s->context, offset, size) : NULL;
  if (p) return p;
  return s->read && s->read(s->context, offset, scratch, size) ? scratch : NULL;
}

int xe_xex_parse(const xe_xex_source *s, xe_xex_metadata *output) {
  uint8_t scratch[256];
  const uint8_t *p;
  uint32_t header_size, count, i, j, n, key, slot, length;
  uint32_t offsets[3], found, offset;
  xe_xex_metadata m;
  if (!s || !output || s->size < 32) return 0;
  p = xex_get(s, 0, 24, scratch, s->size);
  if (!p) return 0;
  key = xex_be32(p);
  if (key != 0x58455831U && key != 0x58455832U) return 0;
  header_size = xex_be32(p + 8);
  count = xex_be32(p + 20);
  if (header_size < 32 || header_size > s->size ||
      count > (header_size - 24) / 8) return 0;
  memset(&m, 0, sizeof(m));
  m.format = key == 0x58455831U ? 1 : 2;
  m.esrb = m.pegi = m.cero = m.usk = m.oflc_au = m.oflc_nz = 255;
  memset(offsets, 0, sizeof(offsets));
  found = 0;
  for (i = 0; i < count && found != 7; i += n) {
    n = count - i;
    if (n > sizeof(scratch) / 8) n = sizeof(scratch) / 8;
    p = xex_get(s, 24 + (uint64_t)i * 8, n * 8, scratch, header_size);
    if (!p) return 0;
    for (j = 0; j < n; ++j) {
      key = xex_be32(p + j * 8);
      slot = key == 0x00040006U ? 0 : key == 0x000183ffU ? 1 :
          key == 0x00040310U ? 2 : 3;
      if (slot < 3 && !(found & (1U << slot))) {
        offsets[slot] = xex_be32(p + j * 8 + 4);
        found |= 1U << slot;
      }
    }
  }
  if (found & 1) {
    p = xex_get(s, offsets[0], 24, scratch, header_size);
    if (!p) return 0;
    m.media_id = xex_be32(p);
    m.version = xex_be32(p + 4);
    m.base_version = xex_be32(p + 8);
    m.title_id = xex_be32(p + 12);
    m.disc_number = p[18]; m.disc_count = p[19];
    m.savegame_id = xex_be32(p + 20);
  }
  if (found & 4) {
    p = xex_get(s, offsets[2], 64, scratch, header_size);
    if (!p) return 0;
    m.ratings_present = 1;
    m.esrb = p[0]; m.pegi = p[1]; m.cero = p[5];
    m.usk = p[6]; m.oflc_au = p[7]; m.oflc_nz = p[8];
  }
  if (found & 2) {
    offset = offsets[1];
    p = xex_get(s, offset, 4, scratch, header_size);
    if (!p) return 0;
    length = xex_be32(p);
    if (length < 4 || length > header_size - offset) return 0;
    offset += 4;
    length -= 4;
    /* Trim in bounded reverse chunks; never allocate a declared header. */
    while (length) {
      n = length > sizeof(scratch) ? sizeof(scratch) : length;
      p = xex_get(s, (uint64_t)offset + length - n, n, scratch, header_size);
      if (!p) return 0;
      j = n;
      while (j && !p[j - 1]) --j;
      length -= n - j;
      if (j) break;
    }
    m.name_offset = offset;
    m.name_size = length;
  }
  *output = m;
  return 1;
}
