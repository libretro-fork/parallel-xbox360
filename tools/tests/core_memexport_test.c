#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "core_memexport.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
static xe_memexport_map map;
static unsigned char reference[XE_MEMEXPORT_PAGE_COUNT];
static xe_memexport_run expected[65536];
static uint32_t random_state = 53251;
static unsigned long queries;
static uint32_t random_next(void) {
  random_state ^= random_state << 13; random_state ^= random_state >> 17; random_state ^= random_state << 5; return random_state;
}
static void reference_change(uint32_t base, uint32_t size, int clear) {
  uint32_t first, end, p;
  if (!size || base >= XE_MEMEXPORT_BUFFER_SIZE) return;
  if (size > XE_MEMEXPORT_BUFFER_SIZE - base) size = XE_MEMEXPORT_BUFFER_SIZE - base;
  first = clear ? (base + 4095) >> 12 : base >> 12;
  end = clear ? (base + size) >> 12 : ((base + size - 1) >> 12) + 1;
  for (p = first; p < end; ++p) reference[p] = clear ? 0 : 1;
}
static int query(uint32_t base, uint32_t size) {
  uint32_t count = 0, p, clipped = size, i;
  int whole, any;
  if (base >= XE_MEMEXPORT_BUFFER_SIZE) clipped = 0;
  else if (clipped > XE_MEMEXPORT_BUFFER_SIZE - base) clipped = XE_MEMEXPORT_BUFFER_SIZE - base;
  if (clipped) for (p = base >> 12; p <= ((base + clipped - 1) >> 12); ++p) {
    uint32_t start, end;
    if (!reference[p]) continue;
    start = p << 12; if (start < base) start = base;
    end = (p + 1) << 12; if (end > base + clipped) end = base + clipped;
    if (count && expected[count - 1].base + expected[count - 1].size == start) expected[count - 1].size += end - start;
    else { CHECK(count < 65536); expected[count].base = start; expected[count++].size = end - start; }
  }
  any = count != 0;
  CHECK(xe_memexport_has(&map, base, size) == any);
  for (whole = 0; whole <= 1; ++whole) {
    xe_memexport_plan plan;
    xe_memexport_cursor direct;
    xe_memexport_run run;
    int made = xe_memexport_plan_make(&map, base, size, whole, &plan);
    CHECK(made == any && plan.count == (any ? (whole ? 1u : count) : 0));
    CHECK(xe_memexport_cursor_make(&map, base, size, whole, &direct) == any);
    if (!made) continue;
    CHECK(plan.cursor.words == map.words);
    if (whole) {
      CHECK(plan.base == base && plan.size == clipped);
      CHECK(xe_memexport_next(&plan.cursor, &run) && run.base == base && run.size == clipped);
      CHECK(xe_memexport_next(&direct, &run) && run.base == base && run.size == clipped);
    } else {
      CHECK(plan.base == expected[0].base);
      CHECK(plan.size == expected[count - 1].base + expected[count - 1].size - expected[0].base);
      for (i = 0; i < count; ++i) {
        CHECK(xe_memexport_next(&plan.cursor, &run));
        CHECK(run.base == expected[i].base && run.size == expected[i].size && run.size);
        CHECK(xe_memexport_next(&direct, &run));
        CHECK(run.base == expected[i].base && run.size == expected[i].size && run.size);
      }
    }
    CHECK(!xe_memexport_next(&plan.cursor, &run));
    CHECK(!xe_memexport_next(&direct, &run));
    CHECK(!xe_memexport_next(&plan.cursor, &run));
  }
  ++queries; return 0;
}
static int check_words(void) {
  uint32_t p;
  for (p = 0; p < XE_MEMEXPORT_PAGE_COUNT; ++p)
    CHECK(((map.words[p >> 6] >> (p & 63)) & 1) == reference[p]);
  return 0;
}
int main(void) {
  uint32_t i, mask, p;
  CHECK(query(0, UINT32_MAX) == 0);
  CHECK(!xe_memexport_mark(&map, XE_MEMEXPORT_BUFFER_SIZE, 1));
  CHECK(!xe_memexport_mark(&map, 0, 0));
  /* All 4,096 patterns across partial first/last bytes and word boundaries. */
  for (mask = 0; mask < 4096; ++mask) {
    xe_memexport_reset(&map); memset(reference, 0, sizeof(reference));
    for (p = 0; p < 12; ++p) if (mask & (1u << p)) {
      uint32_t base = (p + 58) << 12;
      CHECK(xe_memexport_mark(&map, base, 4096)); reference_change(base, 4096, 0);
    }
    CHECK(query((58u << 12) + 17, (12u << 12) - 51) == 0);
    CHECK(query(64u << 12, 4097) == 0);
    CHECK(query(57u << 12, 14u << 12) == 0);
  }
  xe_memexport_reset(&map); memset(reference, 0, sizeof(reference));
  for (i = 0; i < 10000; ++i) {
    uint32_t base = random_next() % XE_MEMEXPORT_BUFFER_SIZE;
    uint32_t size = i % 97 ? random_next() % 65537 : UINT32_MAX;
    int clear = (random_next() & 1) != 0;
    if (clear) xe_memexport_clear(&map, base, size);
    else CHECK(xe_memexport_mark(&map, base, size) == (size != 0));
    reference_change(base, size, clear);
    CHECK(query(base, size) == 0);
    CHECK(query(random_next() % XE_MEMEXPORT_BUFFER_SIZE, random_next() % 262145) == 0);
    if (!(i % 127)) CHECK(check_words() == 0);
  }
  CHECK(check_words() == 0);
  CHECK(query(XE_MEMEXPORT_BUFFER_SIZE - 1, UINT32_MAX) == 0);
  CHECK(query(XE_MEMEXPORT_BUFFER_SIZE, UINT32_MAX) == 0);
  CHECK(query(UINT32_MAX, UINT32_MAX) == 0);
  CHECK(query(0, 0) == 0);
  xe_memexport_reset(&map); memset(reference, 0, sizeof(reference));
  for (i = 0; i < XE_MEMEXPORT_PAGE_COUNT; i += 2) {
    CHECK(xe_memexport_mark(&map, i << 12, 4096)); reference[i] = 1;
  }
  CHECK(query(0, UINT32_MAX) == 0);
  {
    xe_memexport_plan plan;
    CHECK(xe_memexport_plan_make(&map, 0, UINT32_MAX, 0, &plan) && plan.count == 65536);
  }
  /* Clearing partial edge pages preserves neighbouring bytes. */
  xe_memexport_clear(&map, 1, 8190); reference_change(1, 8190, 1);
  CHECK(query(0, 8192) == 0 && check_words() == 0);
  xe_memexport_reset(&map); memset(reference, 0, sizeof(reference));
  CHECK(query(0, UINT32_MAX) == 0);
  printf("Memexport C89: %lu exact partial/whole queries, word-wise mark/clear, 65,536 borrowed runs and byte-clipped barriers match page reference\n", queries);
  return 0;
}
