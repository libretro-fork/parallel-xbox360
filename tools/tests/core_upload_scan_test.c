#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
typedef struct allocation_header { size_t bytes; uint64_t guard; uintmax_t alignment; } allocation_header;
static size_t calls, live, fail_at;
static int bad_guard;
static void *test_alloc(size_t bytes) {
  allocation_header *h;
  uint64_t guard = UINT64_C(0x7654321001234567);
  ++calls; if (calls == fail_at) return NULL;
  h = (allocation_header *)malloc(sizeof(*h) + bytes + sizeof(guard));
  if (!h) return NULL;
  h->bytes = bytes; h->guard = guard; ++live;
  memset(h + 1, 0xa5, bytes);
  memcpy((unsigned char *)(h + 1) + bytes, &guard, sizeof(guard));
  return h + 1;
}
static void test_free(void *data) {
  allocation_header *h;
  uint64_t guard;
  if (!data) return;
  h = (allocation_header *)data - 1;
  memcpy(&guard, (unsigned char *)data + h->bytes, sizeof(guard));
  if (h->guard != UINT64_C(0x7654321001234567) || guard != h->guard) bad_guard = 1;
  --live; free(h);
}
#define XE_UPLOAD_SCAN_ALLOC test_alloc
#define XE_UPLOAD_SCAN_FREE test_free
#include "core_upload_scan.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
static xe_page_valid_word words[2050];
static uint64_t source[2048];
static xe_upload_range expected[65536];
static uint64_t checks;
static void publish(uint32_t pages, uint32_t pattern, unsigned mode) {
  uint32_t i, count = (pages + 63) / 64;
  uint64_t random = UINT64_C(0x5827932a37564219) ^ pattern;
  for (i = 0; i < count; ++i) {
    random ^= random << 13; random ^= random >> 7; random ^= random << 17;
    source[i] = mode == 0 ? pattern : mode == 1 ? UINT64_MAX : mode == 2 ? 0 :
        mode == 3 ? UINT64_C(0xaaaaaaaaaaaaaaaa) : mode == 4 ? UINT64_C(0x5555555555555555) : random;
    xe_page_valid_store(words + i + 1, source[i]);
  }
  xe_page_valid_store(words, UINT64_C(0x123456789abcdef0));
  xe_page_valid_store(words + count + 1, UINT64_C(0xfedcba9876543210));
}
static int check_collect(xe_upload_scan *s, uint32_t first, uint32_t last) {
  uint32_t page, count = 0, start = UINT32_MAX, i;
  for (page = first; page <= last; ++page) {
    int valid = (source[page >> 6] & (UINT64_C(1) << (page & 63))) != 0;
    if (!valid && start == UINT32_MAX) start = page;
    if (valid && start != UINT32_MAX) {
      expected[count].first = start; expected[count++].second = page - start; start = UINT32_MAX;
    }
  }
  if (start != UINT32_MAX) { expected[count].first = start; expected[count++].second = last + 1 - start; }
  CHECK(xe_upload_all_valid(words + 1, s->pages, first, last) == (count == 0));
  CHECK(xe_upload_scan_collect(s, words + 1, first, last));
  CHECK(s->count == count);
  for (i = 0; i < count; ++i) CHECK(s->data[i].first == expected[i].first && s->data[i].second == expected[i].second);
  for (i = 0; i < (s->pages + 63) / 64; ++i) CHECK(xe_page_valid_load(words + i + 1) == source[i]);
  CHECK(xe_page_valid_load(words) == UINT64_C(0x123456789abcdef0));
  CHECK(xe_page_valid_load(words + (s->pages + 63) / 64 + 1) == UINT64_C(0xfedcba9876543210));
  CHECK(!bad_guard); ++checks; return 0;
}
static int check_pages(uint32_t pages) {
  xe_upload_scan s = {0};
  uint32_t mode, i;
  size_t before;
  CHECK(xe_upload_scan_init(&s, pages));
  CHECK(s.capacity == (pages >> 1) + (pages & 1));
  CHECK(s.data[s.capacity - 1].first == UINT32_C(0xa5a5a5a5));
  before = calls;
  for (mode = 1; mode <= 5; ++mode) {
    publish(pages, 419, mode);
    CHECK(check_collect(&s, 0, pages - 1) == 0);
    for (i = 0; i < pages && i < 130; ++i) CHECK(check_collect(&s, i, i) == 0);
    if (pages > 66) {
      CHECK(check_collect(&s, 63, 65) == 0);
      CHECK(check_collect(&s, 1, pages - 2) == 0);
    }
  }
  CHECK(calls == before);
  CHECK(!xe_upload_scan_collect(&s, words + 1, pages, pages) && !s.count);
  CHECK(!xe_upload_scan_collect(&s, NULL, 0, pages - 1) && !s.count);
  CHECK(!xe_upload_scan_collect(&s, words + 1, 1, 0) && !s.count);
  CHECK(!xe_upload_all_valid(words + 1, pages, 0, pages));
  xe_upload_scan_destroy(&s); xe_upload_scan_destroy(&s);
  CHECK(!live && !bad_guard); return 0;
}
int main(void) {
  xe_upload_scan s = {0};
  uint32_t pages, mask, first, last, i, saved_first, saved_last;
  size_t before;
  const uint32_t sizes[] = {1, 2, 63, 64, 65, 127, 128, 129, 32768, 131072};
  CHECK(!xe_upload_scan_init(&s, 0) && !calls);
  fail_at = calls + 1; CHECK(!xe_upload_scan_init(&s, 131072) && !s.data && !live);
  fail_at = 0;
  CHECK(!xe_upload_scan_collect(&s, words, 0, 0));
  for (pages = 1; pages <= 12; ++pages) {
    CHECK(xe_upload_scan_init(&s, pages)); before = calls;
    CHECK(!xe_upload_scan_init(&s, pages));
    for (mask = 0; mask < (1u << pages); ++mask) {
      publish(pages, mask, 0);
      for (first = 0; first < pages; ++first)
        for (last = first; last < pages; ++last) CHECK(check_collect(&s, first, last) == 0);
    }
    CHECK(calls == before); xe_upload_scan_destroy(&s);
  }
  for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) CHECK(check_pages(sizes[i]) == 0);
  CHECK(xe_upload_scan_init(&s, 131072));
  publish(s.pages, 0, 3);
  CHECK(check_collect(&s, 0, s.pages - 1) == 0 && s.count == 65536);
  CHECK(s.data[65535].first == 131070 && s.data[65535].second == 1);
  saved_first = s.data[0].first; saved_last = s.data[65535].first;
  s.capacity = 1;
  CHECK(!xe_upload_scan_collect(&s, words + 1, 0, s.pages - 1) && !s.count);
  CHECK(s.data[0].first == saved_first && s.data[65535].first == saved_last);
  s.capacity = 65536;
  for (i = 0; i < 32; ++i) { publish(s.pages, i, 5); CHECK(check_collect(&s, i * 61, s.pages - 1 - i) == 0); }
  xe_upload_scan_destroy(&s);
  /* A partial final dirty chunk must not touch words beyond its allocation. */
  for (i = 0; i < 3; ++i) { source[i] = UINT64_MAX - i; xe_page_valid_store(words + i, 0); }
  xe_page_valid_store(words + 3, 123);
  xe_page_valid_copy(words, source, 3, 1, 64);
  CHECK(xe_page_valid_load(words) == 0 && xe_page_valid_load(words + 1) == source[1] && xe_page_valid_load(words + 2) == source[2] && xe_page_valid_load(words + 3) == 123);
  xe_page_valid_or(words, UINT64_C(0x8000000000000001));
  xe_page_valid_and(words, UINT64_MAX - 1);
  CHECK(xe_page_valid_load(words) == UINT64_C(0x8000000000000000));
  xe_page_valid_clear(words, 3);
  CHECK(!xe_page_valid_load(words) && !xe_page_valid_load(words + 2) && xe_page_valid_load(words + 3) == 123);
  CHECK(!live && !bad_guard);
  printf("Upload scan C89: %lu exact bitmap/query comparisons, full 65,536 runs, no hot allocation, partial chunks, bounds/OOM and ownership guards pass\n", (unsigned long)checks);
  return 0;
}
