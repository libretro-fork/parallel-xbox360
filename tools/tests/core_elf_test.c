/* ELF wire bounds and transactional guest transfer; MSVC C89. */
#include <stdio.h>
#include "core_elf.h"
static uint8_t image[1024], guest[262144];
static uint8_t large_image[52U + 65535U * 32U];
static unsigned reserves, releases, notifications, translations, invalid, fail_at;
static uint32_t reserved_base[8], reserved_size[8];
#ifdef XE_ELF_BRIDGE
extern int xe_test_elf_bridge(const uint8_t *, size_t);
#endif
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
static void put32(size_t off, uint32_t value) {
  image[off] = (uint8_t)(value >> 24); image[off + 1] = (uint8_t)(value >> 16);
  image[off + 2] = (uint8_t)(value >> 8); image[off + 3] = (uint8_t)value;
}
static void put16(size_t off, unsigned value) {
  image[off] = (uint8_t)(value >> 8); image[off + 1] = (uint8_t)value;
}
static void segment(unsigned i, uint32_t address, uint32_t offset,
    uint32_t file_size, uint32_t memory_size) {
  size_t p = 52 + (size_t)i * 32;
  put32(p, 1); put32(p + 4, offset); put32(p + 8, address);
  put32(p + 16, file_size); put32(p + 20, memory_size);
  put32(p + 24, 5); put32(p + 28, 1);
}
static void make_image(void) {
  unsigned i;
  memset(image, 0, sizeof(image)); memcpy(image, "\177ELF", 4);
  image[4] = 1; image[5] = 2; image[6] = 1;
  put16(16, 2); put16(18, 20); put32(20, 1); put32(24, 0x80000010U);
  put32(28, 52); put16(40, 52); put16(42, 32); put16(44, 3);
  segment(0, 0x80000010U, 512, 31, 40);
  segment(1, 0x80000080U, 600, 17, 32);
  segment(2, 0x80020000U, 700, 33, 64);
  for (i = 512; i < sizeof(image); ++i) image[i] = (uint8_t)(i * 37);
  memset(guest, 0xa7, sizeof(guest));
  reserves = releases = notifications = translations = invalid = fail_at = 0;
}
#ifdef XE_ELF_BRIDGE
void xe_test_elf_tail_image(void) {
  put16(44, 1); put32(52 + 20, 0x40010);
}
#endif
static int reserve_range(void *opaque, uint32_t base, uint32_t size) {
  unsigned i;
  (void)opaque;
  if (reserves >= 8 || !size) { invalid = 1; return 0; }
  for (i = 0; i < sizeof(guest); ++i) if (guest[i] != 0xa7) invalid = 1;
  ++reserves;
  if (reserves == fail_at) return 0;
  reserved_base[reserves - 1] = base; reserved_size[reserves - 1] = size;
  return 1;
}
static void release_range(void *opaque, uint32_t base, uint32_t size) {
  (void)opaque;
  if (releases >= reserves || base != reserved_base[releases] ||
      size != reserved_size[releases]) invalid = 1;
  ++releases;
}
static uint8_t *translate(void *opaque, uint32_t address) {
  (void)opaque; ++translations;
  if (fail_at == 99 && translations == 2) return NULL;
  if (fail_at == 100) return image + 8;
  if (address < 0x80000000U || address - 0x80000000U >= sizeof(guest)) {
    invalid = 1; return NULL;
  }
  return guest + address - 0x80000000U;
}
static void executable(void *opaque, uint32_t base, uint32_t end) {
  (void)opaque;
  if (!end || base >= end || (base & 3) || (end & 3)) invalid = 1;
  ++notifications;
}
int main(void) {
  xe_elf_plan p;
  xe_elf_cursor cursor;
  xe_elf_sink sink;
  xe_elf_state state;
  uint32_t base, size, after, rng = 12345;
  size_t i, n;
  unsigned fault;
  uint32_t addresses[8], lengths[8], value, page_address, end;
  unsigned j, k;
  uint8_t expected_pages[128], actual_pages[128];
  sink.opaque = NULL; sink.reserve = reserve_range; sink.release = release_range;
  sink.translate = translate; sink.executable = executable;
  make_image();
  CHECK(xe_elf_validate(&p, image, sizeof(image), 65536, 4096));
  memset(&cursor, 0, sizeof(cursor));
  CHECK(xe_elf_range_next(&p, &cursor, &base, &size) && base == 0x80000000U && size == 65536);
  CHECK(xe_elf_range_next(&p, &cursor, &base, &size) && base == 0x80020000U && size == 65536);
  CHECK(!xe_elf_range_next(&p, &cursor, &base, &size));
  state.reserved = 0;
  CHECK(xe_elf_apply(&p, &sink, &state));
  CHECK(!state.reserved && !invalid && reserves == 2 && !releases && notifications == 3);
  CHECK(!memcmp(guest + 16, image + 512, 31));
  for (i = 47; i < 56; ++i) CHECK(guest[i] == 0);
  CHECK(guest[15] == 0xa7 && guest[56] == 0xa7);
  CHECK(!memcmp(guest + 128, image + 600, 17) && guest[160] == 0xa7);
  for (fault = 1; fault <= 4; ++fault) {
    make_image(); CHECK(xe_elf_validate(&p, image, sizeof(image), 65536, 4096));
    fail_at = fault >= 3 ? 96 + fault : fault; state.reserved = 0;
    CHECK(!xe_elf_apply(&p, &sink, &state));
    CHECK(!state.reserved && !invalid && !notifications);
    CHECK(releases == (fault == 4 ? 0u : fault == 3 ? 1u : fault - 1));
    for (i = 0; i < sizeof(guest); ++i) CHECK(guest[i] == 0xa7);
  }
  /* A later malformed load must fail the entire preflight. */
  for (fault = 0; fault < 13; ++fault) {
    make_image();
    switch (fault) {
      case 0: image[5] = 1; break;
      case 1: put16(42, 31); break;
      case 2: put32(28, 1000); break;
      case 3: put16(44, 65535); break;
      case 4: put32(52 + 64 + 4, 1020); break;
      case 5: put32(52 + 64 + 20, 1); break;
      case 6: put32(52 + 64 + 8, 0x9ffffff0U); break;
      case 7: put32(52 + 64 + 28, 3); break;
      case 8: put32(52 + 64 + 8, 0x7fffffffU); break;
      case 9: put32(52 + 64 + 8, 0x80000000U); break;
      case 10: put32(24, 0x90000000U); break;
      case 11: put16(40, 1025); break;
      default: put32(52 + 64 + 28, 4096); break;
    }
    CHECK(!xe_elf_validate(&p, image, sizeof(image), 65536, 4096));
  }
  make_image();
  for (i = 0; i < 52; ++i) CHECK(!xe_elf_validate(&p, image, i, 65536, 4096));
  CHECK(!xe_elf_validate(&p, NULL, 1024, 65536, 4096));
  /* Heap split, overlapping pages, zero align, PT_DYNAMIC metadata ignored. */
  put32(24, 0x8ffffff0U); put16(44, 2);
  segment(0, 0x8ffffff0U, 512, 31, 8192); put32(52 + 28, 0);
  put32(84, 2); put32(84 + 8, 0); put32(84 + 16, 0xffffffffU);
  CHECK(xe_elf_validate(&p, image, sizeof(image), 65536, 4096));
  memset(&cursor, 0, sizeof(cursor));
  CHECK(xe_elf_range_next(&p, &cursor, &base, &size) && base == 0x8fff0000U && size == 65536);
  CHECK(xe_elf_range_next(&p, &cursor, &base, &size) && base == 0x90000000U && size == 8192);
  /* Deterministic mutations exercise wire/range arithmetic under sanitizers. */
  for (n = 0; n < 20000; ++n) {
    make_image();
    for (i = 0; i < 8; ++i) {
      rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
      image[rng % 160] ^= (uint8_t)(rng >> 24);
    }
    if (xe_elf_validate(&p, image, sizeof(image), 65536, 4096)) {
      memset(&cursor, 0, sizeof(cursor)); after = 0; fault = 0;
      while (xe_elf_range_next(&p, &cursor, &base, &size)) {
        CHECK(size && base >= after && base >= 0x80000000U && size <= 0xa0000000U - base);
        after = base + size; CHECK(++fault < 16);
      }
    }
  }
  /* Independent 4-KiB occupancy oracle around the 64-KiB/4-KiB heap boundary. */
  for (n = 0; n < 1000; ++n) {
    make_image(); put16(44, 8);
    memset(expected_pages, 0, sizeof(expected_pages));
    memset(actual_pages, 0, sizeof(actual_pages));
    for (j = 0; j < 8; ++j) {
      rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
      addresses[j] = 0x8ffc0000U + (rng % 120) * 4096 + (rng % 17);
    }
    for (j = 1; j < 8; ++j) {
      value = addresses[j]; k = j;
      while (k && addresses[k - 1] > value) { addresses[k] = addresses[k - 1]; --k; }
      addresses[k] = value;
    }
    put32(24, addresses[0]);
    for (j = 0; j < 8; ++j) {
      rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
      lengths[j] = 8 + rng % 8192;
      segment(j, addresses[j], 512, 8, lengths[j]);
      for (page_address = addresses[j] & ~4095U;
           page_address < addresses[j] + lengths[j]; page_address += 4096) {
        base = page_address < 0x90000000U ? page_address & ~65535U : page_address;
        end = base + (page_address < 0x90000000U ? 65536 : 4096);
        for (; base < end; base += 4096)
          expected_pages[(base - 0x8ffc0000U) / 4096] = 1;
      }
    }
    CHECK(xe_elf_validate(&p, image, sizeof(image), 65536, 4096));
    memset(&cursor, 0, sizeof(cursor)); after = 0; fault = 0;
    while (xe_elf_range_next(&p, &cursor, &base, &size)) {
      CHECK(base >= after && size && ++fault <= 16);
      end = base + size; after = end;
      for (; base < end; base += 4096) {
        CHECK(base >= 0x8ffc0000U && base < 0x90040000U);
        i = (base - 0x8ffc0000U) / 4096;
        CHECK(!actual_pages[i]); actual_pages[i] = 1;
      }
    }
    CHECK(!memcmp(expected_pages, actual_pages, sizeof(expected_pages)));
  }
  make_image();
  memcpy(large_image, image, 52);
  large_image[44] = large_image[45] = 255;
  memcpy(large_image + sizeof(large_image) - 32, image + 52, 32);
  CHECK(xe_elf_validate(&p, large_image, sizeof(large_image), 65536, 4096));
  memset(&cursor, 0, sizeof(cursor));
  CHECK(xe_elf_range_next(&p, &cursor, &base, &size) && base == 0x80000000U && size == 65536);
  CHECK(!xe_elf_range_next(&p, &cursor, &base, &size));
  puts("ELF: borrowed wire validation, merged heap pages, direct copy/BSS and failure rollback pass");
#ifdef XE_ELF_BRIDGE
  make_image();
  segment(1, 0x80040000U, 600, 17, 32);
  segment(2, 0x80060000U, 700, 33, 64);
  put32(52 + 24, 0); put32(84 + 24, 0); put32(116 + 24, 0);
  CHECK(xe_test_elf_bridge(image, sizeof(image)));
  puts("Actual ElfModule: occupied pages preserved, failed allocations rolled back, successful load and state pass");
#endif
  return 0;
}
