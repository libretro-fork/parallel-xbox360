#include "libretro/core_guest_hash.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
void xe_legacy_sha256_update(uint32_t *, uint8_t *, uint32_t *, const uint8_t *, size_t);
void xe_legacy_sha256_final(const uint32_t *, const uint8_t *, uint32_t, uint8_t *);
static uint32_t be32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
      ((uint32_t)p[2] << 8) | p[3];
}
static void put32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
  p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}
static void reference(uint8_t *guest, const uint8_t *input, size_t size,
    uint8_t *out, size_t cap, int final) {
  uint32_t words[8], count = be32(guest);
  uint8_t buffer[64], digest[32];
  unsigned i;
  for (i = 0; i < 8; ++i) words[i] = be32(guest + 4 + i * 4);
  memcpy(buffer, guest + 36, 64);
  if (final) {
    xe_legacy_sha256_final(words, buffer, count, digest);
    if (cap) memcpy(out, digest, cap < 32 ? cap : 32);
    memcpy(guest + 36, digest, 32);
  } else {
    xe_legacy_sha256_update(words, buffer, &count, input, size);
    for (i = 0; i < 8; ++i) put32(guest + 4 + i * 4, words[i]);
    memcpy(guest + 36, buffer, 64); put32(guest, count);
  }
}
static uint8_t data[65537], current[180], old[180], saved[180];
static void check_final(void) {
  static const unsigned caps[] = {0,1,15,31,32,33,40};
  static const unsigned offsets[] = {0,4,12,36,52,92};
  uint8_t a[64], b[64];
  unsigned i, j;
  memcpy(saved, current, sizeof(saved));
  for (i = 0; i < sizeof(caps) / sizeof(caps[0]); ++i) {
    memcpy(current, saved, sizeof(saved)); memcpy(old, saved, sizeof(saved));
    memset(a, 0xa7, sizeof(a)); memset(b, 0xa7, sizeof(b));
    reference(old + 7, NULL, 0, b + 3, caps[i], 1);
    assert(xe_guest_sha256_final(current + 7, a + 3, caps[i]));
    assert(!memcmp(a, b, sizeof(a)) && !memcmp(current, old, sizeof(old)));
    /* Finalization and further updates remain guest-visible, even repeated. */
    reference(old + 7, NULL, 0, b + 3, caps[i], 1);
    assert(xe_guest_sha256_final(current + 7, a + 3, caps[i]));
    assert(!memcmp(a, b, sizeof(a)) && !memcmp(current, old, sizeof(old)));
    reference(old + 7, data, 5, NULL, 0, 0);
    assert(xe_guest_sha256_update(current + 7, data, 5));
    assert(!memcmp(current, old, sizeof(old)));
    for (j = 0; j < sizeof(offsets) / sizeof(offsets[0]); ++j) {
      memcpy(current, saved, sizeof(saved)); memcpy(old, saved, sizeof(saved));
      reference(old + 7, NULL, 0, old + 7 + offsets[j], caps[i], 1);
      assert(xe_guest_sha256_final(current + 7, current + 7 + offsets[j], caps[i]));
      assert(!memcmp(current, old, sizeof(old)));
    }
  }
}
int main(void) {
  static const uint8_t abc[32] = {
    0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
    0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad
  };
  static const unsigned chunks[] = {1,3,17,64,65,127,4096};
  static const uint32_t counts[] = {0x1ffffff0U,0xfffffff0U,0xffffffffU};
  uint32_t random = 0x12345678U;
  size_t i, length, at, size;
  unsigned test, chunk, offset;
  assert(xe_guest_sha256_init(current + 7));
  assert(xe_guest_sha256_update(current + 7, (const uint8_t *)"abc", 3));
  assert(xe_guest_sha256_final(current + 7, current + 120, 32));
  assert(!memcmp(current + 120, abc, sizeof(abc)));
  for (i = 0; i < sizeof(data); ++i) {
    random ^= random << 13; random ^= random >> 17; random ^= random << 5;
    data[i] = (uint8_t)random;
  }
  for (test = 0; test < 330; ++test) {
    length = test < 260 ? test : (test - 259) * 907U;
    for (chunk = 0; chunk < sizeof(chunks) / sizeof(chunks[0]); ++chunk) {
      memset(current, 0xa7, sizeof(current));
      assert(xe_guest_sha256_init(current + 7));
      memcpy(old, current, sizeof(old));
      for (at = 0; at < length; at += size) {
        size = length - at; if (size > chunks[chunk]) size = chunks[chunk];
        reference(old + 7, data + 1 + at, size, NULL, 0, 0);
        assert(xe_guest_sha256_update(current + 7, data + 1 + at, size));
        assert(!memcmp(current, old, sizeof(old)));
      }
      if (chunk == 3) check_final();
    }
  }
  for (test = 0; test < sizeof(counts) / sizeof(counts[0]); ++test) {
    assert(xe_guest_sha256_init(current + 7));
    put32(current + 7, counts[test]); memcpy(current + 43, data, 64);
    memcpy(old, current, sizeof(old));
    reference(old + 7, data, 80, NULL, 0, 0);
    assert(xe_guest_sha256_update(current + 7, data, 80));
    assert(!memcmp(current, old, sizeof(old))); check_final();
  }
  for (offset = 0; offset < 100; ++offset) {
    assert(xe_guest_sha256_init(current + 7));
    put32(current + 7, offset % 64); memcpy(current + 43, data, 64);
    memcpy(old, current, sizeof(old));
    reference(old + 7, old + offset, 17, NULL, 0, 0);
    assert(xe_guest_sha256_update(current + 7, current + offset, 17));
    assert(!memcmp(current, old, sizeof(old)));
  }
  memcpy(saved, current, sizeof(saved));
  assert(!xe_guest_sha256_update(current + 7, NULL, 1));
  assert(!xe_guest_sha256_final(current + 7, NULL, 1));
  assert(!memcmp(saved, current, sizeof(saved)));
  puts("guest SHA-256 exact state, aliasing, repeated final and count wrap: PASS");
  return 0;
}
