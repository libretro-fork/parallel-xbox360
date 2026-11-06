#include <stdio.h>
#include <stdint.h>
#include "core_upload_ranges.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
typedef char upload_range_size_check[sizeof(xe_upload_range) == 8 ? 1 : -1];
typedef struct guarded_ranges {
  uint64_t before;
  xe_upload_range ranges[65536];
  uint64_t after;
} guarded_ranges;
static guarded_ranges scratch;
static uint32_t selected[65536];
static int check_case(uint32_t count, uint32_t mask, unsigned pattern) {
  uint32_t i, expected_count = 0, written = 0, moved = 0, prefix = 0;
  int hole = 0;
  scratch.before = UINT64_C(0x8765432112345678); scratch.after = ~scratch.before;
  for (i = 0; i < count; ++i) {
    scratch.ranges[i].first = i * 4;
    scratch.ranges[i].second = (i & 3) + 1;
  }
  if (count < 65536) { scratch.ranges[count].first = UINT32_MAX; scratch.ranges[count].second = UINT32_MAX; }
  for (i = 0; i < count; ++i) {
    int keep;
    /* Earlier compaction writes must never clobber the next callback input. */
    CHECK(scratch.ranges[i].first == i * 4 && scratch.ranges[i].second == (i & 3) + 1);
    keep = pattern == 0 ? (int)((mask >> i) & 1u) :
        pattern == 1 ? 1 : pattern == 2 ? 0 : pattern == 3 ? (int)(i & 1) :
        pattern == 4 ? (i < count / 2) : pattern == 5 ? (i >= count / 2) :
        ((i * 73u + mask) % 17u != 0);
    if (!keep) { hole = 1; continue; }
    if (!hole) ++prefix;
    selected[expected_count++] = i;
    moved += (uint32_t)xe_upload_range_keep(scratch.ranges, i, &written);
  }
  CHECK(written == expected_count && moved == written - prefix);
  for (i = 0; i < written; ++i) {
    CHECK(scratch.ranges[i].first == selected[i] * 4);
    CHECK(scratch.ranges[i].second == (selected[i] & 3) + 1);
  }
  CHECK(scratch.before == UINT64_C(0x8765432112345678) && scratch.after == ~scratch.before);
  if (count < 65536) CHECK(scratch.ranges[count].first == UINT32_MAX && scratch.ranges[count].second == UINT32_MAX);
  return 0;
}
int main(void) {
  uint32_t count, mask;
  unsigned pattern;
  for (count = 0; count <= 16; ++count)
    for (mask = 0; mask < (1u << count); ++mask) CHECK(check_case(count, mask, 0) == 0);
  for (pattern = 1; pattern <= 6; ++pattern) {
    CHECK(check_case(65536, 19, pattern) == 0);
    CHECK(check_case(65, 47, pattern) == 0);
  }
  puts("Upload ranges C89: 131,071 exhaustive masks, full 65,536-record spans, exact stable selection, unchanged prefix, callback-input ownership and canaries pass");
  return 0;
}
