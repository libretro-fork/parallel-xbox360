#include "libretro/core_xex_metadata.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef XE_XEX_CPP_BRIDGE
int xe_test_xex_projection(const uint8_t *data, size_t size);
#endif
typedef struct fixture {
  uint8_t data[512];
  size_t bytes;
  int fail;
} fixture;
static void put(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
  p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}
static int read_at(void *opaque, uint64_t at, void *out, size_t size) {
  fixture *f = (fixture *)opaque;
  assert(at <= sizeof(f->data) && size <= sizeof(f->data) - (size_t)at);
  if (f->fail) return 0;
  f->bytes += size;
  memcpy(out, f->data + (size_t)at, size);
  return 1;
}
static const uint8_t *view_at(void *opaque, uint64_t at, size_t size) {
  fixture *f = (fixture *)opaque;
  assert(at <= sizeof(f->data) && size <= sizeof(f->data) - (size_t)at);
  return f->data + (size_t)at;
}
int main(void) {
  fixture f;
  xe_xex_source s;
  xe_xex_metadata m, before;
  int mode;
  uint32_t at;
  memset(&f, 0, sizeof(f));
  s.context = &f; s.size = sizeof(f.data); s.read = read_at; s.view = NULL;
  put(f.data, 0x58455832U); put(f.data + 8, 512); put(f.data + 20, 4);
  put(f.data + 24, 0x40006); put(f.data + 28, 64);
  put(f.data + 32, 0x183ff); put(f.data + 36, 100);
  put(f.data + 40, 0x40310); put(f.data + 44, 128);
  /* First duplicate wins, even when the later value is invalid. */
  put(f.data + 48, 0x40006); put(f.data + 52, 0xffffffffU);
  put(f.data + 64, 123); put(f.data + 68, 0x12345678);
  put(f.data + 72, 0x87654321U); put(f.data + 76, 456);
  f.data[82] = 1; f.data[83] = 2; put(f.data + 84, 789);
  put(f.data + 100, 12); memcpy(f.data + 104, "a\0bc", 4);
  f.data[128] = 7; f.data[129] = 8; f.data[133] = 9;
  f.data[134] = 10; f.data[135] = 11; f.data[136] = 12;
#ifdef XE_XEX_CPP_BRIDGE
  assert(xe_test_xex_projection(f.data, sizeof(f.data)));
#endif
  for (mode = 0; mode < 2; ++mode) {
    s.view = mode ? view_at : NULL;
    assert(xe_xex_parse(&s, &m));
    assert(m.format == 2 && m.media_id == 123 && m.title_id == 456);
    assert(m.savegame_id == 789 && m.version == 0x12345678);
    assert(m.base_version == 0x87654321U && m.disc_number == 1 && m.disc_count == 2);
    assert(m.name_offset == 104 && m.name_size == 4);
    assert(m.ratings_present && m.esrb == 7 && m.pegi == 8 && m.cero == 9);
    assert(m.usk == 10 && m.oflc_au == 11 && m.oflc_nz == 12);
    before = m;
    put(f.data + 28, 500); assert(!xe_xex_parse(&s, &m));
    assert(!memcmp(&m, &before, sizeof(m))); put(f.data + 28, 64);
    put(f.data + 100, 0xffffffffU); assert(!xe_xex_parse(&s, &m));
    put(f.data + 100, 3); assert(!xe_xex_parse(&s, &m));
    put(f.data + 100, 12);
    put(f.data + 20, 0xffffffffU); assert(!xe_xex_parse(&s, &m));
    put(f.data + 20, 4);
  }
  s.view = NULL; f.fail = 1; assert(!xe_xex_parse(&s, &m)); f.fail = 0;
  /* A sparse, multi-gigabyte declared header with only three fields must not
   * allocate or read that header. All needed bytes remain in this fixture. */
  s.size = 0xffffffffU; put(f.data + 8, 0xffffffffU); f.bytes = 0;
  assert(xe_xex_parse(&s, &m)); assert(f.bytes < 256);
  put(f.data, 0x58455831U); assert(xe_xex_parse(&s, &m) && m.format == 1);
  /* Tail trimming must span multiple chunks and retain embedded NULs. */
  s.size = 512; put(f.data + 8, 512); put(f.data + 20, 2);
  memset(f.data + 104, 0, sizeof(f.data) - 104);
  memcpy(f.data + 104, "a\0bc", 4); put(f.data + 100, 404);
  assert(xe_xex_parse(&s, &m) && m.name_size == 4);
  memset(f.data + 104, 0, 4);
  assert(xe_xex_parse(&s, &m) && !m.name_size);
  /* Sweep every execution payload endpoint, including partial tail spans. */
  put(f.data + 20, 1);
  for (at = 480; at < 520; ++at) {
    put(f.data + 28, at);
    assert(xe_xex_parse(&s, &m) == (at <= 512 - 24));
  }
  s.size = 31; assert(!xe_xex_parse(&s, &m));
  assert(!xe_xex_parse(NULL, &m)); assert(!xe_xex_parse(&s, NULL));
  puts("bounded XEX metadata: PASS");
  return 0;
}
