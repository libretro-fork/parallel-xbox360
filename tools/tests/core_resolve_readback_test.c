/* CPU-only native record and range reference; no Vulkan device is created. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <vulkan/vulkan.h>
static size_t allocations, frees;
static int fail_alloc;
static void *test_alloc(size_t n) { void *p; if (fail_alloc) return NULL; p = malloc(n); if (p) { ++allocations; memset(p, 0xa5, n); } return p; }
static void test_free(void *p) { if (p) ++frees; free(p); }
#define XE_RESOLVE_ALLOC test_alloc
#define XE_RESOLVE_FREE test_free
#include "core_resolve_readback.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); return 1; } } while (0)
static xe_memexport_run *reference;
static uint32_t reference_count, reference_bytes;
static size_t queries;
static void make_reference(const xe_resolve_readback *s, uint64_t key,
    uint32_t base, uint32_t size, int low, int high) {
  uint32_t first, last, page, end;
  reference_count = reference_bytes = 0;
  if (!size || base >= XE_MEMEXPORT_BUFFER_SIZE || !key) return;
  if (size > XE_MEMEXPORT_BUFFER_SIZE - base) size = XE_MEMEXPORT_BUFFER_SIZE - base;
  first = base / 4096; end = base + size; last = (end - 1) / 4096;
  for (page = first; page <= last; ++page) {
    uint32_t at = page * 4096;
    if (xe_resolve_owner_load(s, page) != key) continue;
    if (reference_count && reference[reference_count - 1].base + reference[reference_count - 1].size == at)
      reference[reference_count - 1].size += 4096;
    else { reference[reference_count].base = at; reference[reference_count].size = 4096; ++reference_count; }
  }
  if (reference_count) {
    xe_memexport_run *a = reference, *b = reference + reference_count - 1;
    if (low && a->base == first * 4096) { a->size -= base - a->base; a->base = base; }
    if (high && b->base + b->size == (last + 1) * 4096) b->size -= (last + 1) * 4096 - end;
  }
  for (page = 0; page < reference_count; ++page) reference_bytes += reference[page].size;
}
static int query(xe_resolve_readback *s, uint64_t key, uint32_t base,
    uint32_t size, int low, int high, int mutate) {
  xe_resolve_copy_plan plan;
  xe_resolve_copy_cursor cursor;
  xe_memexport_run range;
  const VkBufferCopy *regions = (const VkBufferCopy *)s->records;
  uint32_t i, mode;
  size_t before = allocations;
  make_reference(s, key, base, size, low, high);
  for (mode = 0; mode < 2; ++mode) {
    uint64_t packed = 0;
    int result = xe_resolve_snapshot_make(s, key, base, size, low, high, mode != 0, &plan);
    CHECK(result == (reference_count != 0) && plan.count == reference_count && plan.bytes == reference_bytes);
    CHECK(plan.count <= XE_RESOLVE_MAX_RUNS && allocations == before);
    if (!result) continue;
    if (mode && mutate) xe_resolve_owner_set(s, base, size, UINT64_C(0xeeeeeeeeeeeeeeee));
    cursor = xe_resolve_cursor_make(&plan); i = 0;
    while (xe_resolve_next(&cursor, &range)) {
      CHECK(i < reference_count && range.base == reference[i].base && range.size == reference[i].size);
      if (s->stride == XE_RESOLVE_NATIVE_STRIDE) {
        CHECK(regions[i].srcOffset == range.base && regions[i].size == range.size);
        CHECK(regions[i].dstOffset == (mode ? packed : range.base));
      }
      packed += range.size; ++i;
    }
    CHECK(i == reference_count && packed == plan.bytes);
  }
  ++queries; return 0;
}

static int packed_delivery(xe_resolve_readback *s) {
  unsigned char *guest, *expected, *source;
  xe_resolve_copy_plan plan;
  xe_resolve_copy_cursor cursor;
  xe_memexport_run range;
  uint32_t i, at = 0, base = 31, length = 16 * 4096 - 98;
  guest = (unsigned char *)malloc(16 * 4096); expected = (unsigned char *)malloc(16 * 4096); source = (unsigned char *)malloc(16 * 4096);
  CHECK(guest && expected && source);
  memset(guest, 0xbd, 16 * 4096); memset(expected, 0xbd, 16 * 4096);
  for (i = 0; i < 16 * 4096; ++i) source[i] = (unsigned char)(i * 7 + 3);
  xe_resolve_owner_set(s, 0, 16 * 4096, 99);
  xe_resolve_owner_set(s, 4 * 4096, 3 * 4096, 100);
  make_reference(s, 99, base, length, 1, 1);
  CHECK(xe_resolve_snapshot_make(s, 99, base, length, 1, 1, 1, &plan));
  cursor = xe_resolve_cursor_make(&plan);
  while (xe_resolve_next(&cursor, &range)) { memcpy(guest + range.base, source + at, range.size); at += range.size; }
  CHECK(at == plan.bytes); at = 0;
  for (i = 0; i < reference_count; ++i) { memcpy(expected + reference[i].base, source + at, reference[i].size); at += reference[i].size; }
  CHECK(!memcmp(guest, expected, 16 * 4096));
  free(guest); free(expected); free(source); return 0;
}
int main(void) {
  xe_resolve_readback s;
  uint32_t i, mode, low, high, random = 0x679123;
  const uint64_t key = UINT64_C(0xfedcba9876543210);
  memset(&s, 0xa5, sizeof(s)); s.owners = NULL; s.records = NULL;
  fail_alloc = 1; CHECK(!xe_resolve_readback_init(&s, sizeof(VkBufferCopy)) && !s.owners);
  fail_alloc = 0; CHECK(!xe_resolve_readback_init(&s, (size_t)-1));
  CHECK(xe_resolve_readback_init(&s, sizeof(VkBufferCopy)) && !xe_resolve_readback_init(&s, 0));
  CHECK((uintptr_t)s.records % sizeof(uint64_t) == 0);
  reference = (xe_memexport_run *)malloc(sizeof(*reference) * XE_RESOLVE_MAX_RUNS); CHECK(reference);
  for (i = 0; i < XE_MEMEXPORT_PAGE_COUNT; ++i) CHECK(xe_resolve_owner_load(&s, i) == 0);
  for (mode = 0; mode < 4; ++mode) {
    for (i = 0; i < XE_MEMEXPORT_PAGE_COUNT; ++i)
      retro_atomic_store_relaxed_64(s.owners + i, (int64_t)((mode == 0 || (mode == 1 && (i & 1)) || (mode == 2 && !(i & 1))) ? key : 1));
    for (low = 0; low < 2; ++low) for (high = 0; high < 2; ++high) {
      CHECK(query(&s, key, 0, XE_MEMEXPORT_BUFFER_SIZE, low, high, 0) == 0);
      CHECK(query(&s, key, 31, XE_MEMEXPORT_BUFFER_SIZE - 72, low, high, 0) == 0);
      CHECK(query(&s, key, 4095, 1, low, high, 0) == 0);
      CHECK(query(&s, key, 63 * 4096 + 17, 2 * 4096 - 29, low, high, 0) == 0);
    }
  }
  for (i = 0; i < 12000; ++i) {
    uint32_t base, length, page, first;
    random = random * UINT32_C(1664525) + UINT32_C(1013904223); base = random % (XE_MEMEXPORT_BUFFER_SIZE - 128 * 4096);
    random = random * UINT32_C(1664525) + UINT32_C(1013904223); length = random % (128 * 4096);
    first = base >> 12;
    for (page = first; page <= ((base + length) >> 12); ++page) {
      random = random * UINT32_C(1664525) + UINT32_C(1013904223);
      retro_atomic_store_relaxed_64(s.owners + page, (int64_t)((random >> 30) ? key : UINT64_C(0xabcdef0012345678)));
    }
    CHECK(query(&s, key, base, length, i & 1, (i >> 1) & 1, 1) == 0);
  }
  xe_resolve_owner_set(&s, XE_MEMEXPORT_BUFFER_SIZE - 3, UINT32_MAX, key);
  CHECK(query(&s, key, XE_MEMEXPORT_BUFFER_SIZE - 3, UINT32_MAX, 1, 1, 0) == 0);
  CHECK(query(&s, key, UINT32_MAX, UINT32_MAX, 1, 1, 0) == 0);
  CHECK(query(&s, key, 0, 0, 0, 0, 0) == 0);
  CHECK(query(&s, 0, 0, 4096, 0, 0, 0) == 0);
  CHECK(packed_delivery(&s) == 0);
  xe_resolve_readback_destroy(&s); CHECK(!s.owners && !s.records && allocations == frees);
  CHECK(xe_resolve_readback_init(&s, 0) && s.records && s.stride == sizeof(xe_memexport_run));
  CHECK(packed_delivery(&s) == 0);
  for (i = 0; i < XE_MEMEXPORT_PAGE_COUNT; ++i)
    retro_atomic_store_relaxed_64(s.owners + i, (int64_t)((i & 1) ? 1 : key));
  for (low = 0; low < 2; ++low) for (high = 0; high < 2; ++high) {
    CHECK(query(&s, key, 0, XE_MEMEXPORT_BUFFER_SIZE, low, high, 0) == 0);
    CHECK(query(&s, key, 31, XE_MEMEXPORT_BUFFER_SIZE - 98, low, high, 0) == 0);
  }
  for (i = 0; i < 3000; ++i) {
    uint32_t page, base, length;
    random = random * UINT32_C(1664525) + UINT32_C(1013904223); base = random % (XE_MEMEXPORT_BUFFER_SIZE - 128 * 4096);
    random = random * UINT32_C(1664525) + UINT32_C(1013904223); length = random % (128 * 4096);
    for (page = base >> 12; page <= ((base + length) >> 12); ++page) {
      random = random * UINT32_C(1664525) + UINT32_C(1013904223);
      retro_atomic_store_relaxed_64(s.owners + page, (int64_t)((random >> 30) ? key : 0));
    }
    CHECK(query(&s, key, base, length, i & 1, (i >> 1) & 1, 1) == 0);
  }
  xe_resolve_readback_destroy(&s); CHECK(allocations == frees); free(reference);
  printf("Resolve readback C89: %lu exact snapshot queries, native host/packed records, 65,536 runs, clipped delivery, key mutation after capture, OOM/overflow/reset pass\n", (unsigned long)queries);
  return 0;
}
