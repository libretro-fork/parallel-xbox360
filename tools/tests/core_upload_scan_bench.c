/* CPU reference only. Legacy shifts are explicitly masked, avoiding its C++
 * undefined behavior; BMI feature dispatch and native locks are not simulated. */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "core_upload_scan.h"
static uint64_t raw[2048];
static xe_page_valid_word atomic_words[2048];
static xe_upload_range legacy_output[65536];
#ifdef _MSC_VER
#define NOINLINE __declspec(noinline)
#elif defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 9
#define NOINLINE __attribute__((noinline,noclone,noipa))
#else
#define NOINLINE __attribute__((noinline))
#endif
/* Preserve a query boundary: GCC must not fold repeated raw snapshots while
 * keeping the new atomic reads. Actual RequestRange is also out of line. */
static NOINLINE int legacy_all_valid(uint32_t first, uint32_t last) {
  uint32_t i;
  for (i = first >> 6; i <= (last >> 6); ++i) {
    uint64_t valid = raw[i];
    if (i == (first >> 6)) valid |= (UINT64_C(1) << (first & 63)) - 1;
    if (i == (last >> 6) && (last & 63) != 63) valid |= ~((UINT64_C(1) << ((last & 63) + 1)) - 1);
    if (valid != UINT64_MAX) return 0;
  }
  return 1;
}
static NOINLINE int legacy_collect(uint32_t first, uint32_t last, uint32_t *count) {
  uint32_t i, start = UINT32_MAX;
  *count = 0;
  for (i = first >> 6; i <= (last >> 6); ++i) {
    uint64_t valid = raw[i];
    if (i == (first >> 6)) valid |= (UINT64_C(1) << (first & 63)) - 1;
    if (i == (last >> 6) && (last & 63) != 63) valid |= ~((UINT64_C(1) << ((last & 63) + 1)) - 1);
    for (;;) {
      uint64_t ends;
      uint32_t bit;
      if (start == UINT32_MAX) {
        if (!~valid) break;
        start = (i << 6) + compat_ctz_u64(~valid);
      }
      ends = valid;
      if (i == (start >> 6)) ends &= ~((UINT64_C(1) << (start & 63)) - 1);
      if (!ends) break;
      bit = compat_ctz_u64(ends);
      if (*count + 1 >= 65536) return 0;
      legacy_output[*count].first = start; legacy_output[*count].second = (i << 6) + bit - start;
      ++*count; valid |= (UINT64_C(1) << bit) - 1; start = UINT32_MAX;
    }
  }
  if (start != UINT32_MAX) {
    legacy_output[*count].first = start; legacy_output[*count].second = last + 1 - start; ++*count;
  }
  return 1;
}
static NOINLINE int stable_all_valid(uint32_t first, uint32_t last) {
  return xe_upload_all_valid_unchecked(atomic_words, first, last);
}
static NOINLINE int stable_collect(xe_upload_scan *s, uint32_t first, uint32_t last) {
  return xe_upload_scan_collect(s, atomic_words, first, last);
}
static NOINLINE uint64_t consume(const xe_upload_range *ranges, uint32_t count) {
  uint32_t i;
  uint64_t sum = count;
  for (i = 0; i < count; ++i) sum += (uint64_t)ranges[i].first * 131 + ranges[i].second;
  return sum;
}
static void prepare(unsigned mode) {
  uint32_t i;
  for (i = 0; i < 2048; ++i) {
    raw[i] = mode == 0 ? UINT64_MAX : mode == 1 ? 0 : mode == 2 ?
        UINT64_C(0xaaaaaaaaaaaaaaaa) : (i & 1) ? UINT64_MAX : UINT64_MAX - 1;
    xe_page_valid_store(atomic_words + i, raw[i]);
  }
}
typedef struct result { double ms; uint64_t sum; } result;
static result run(unsigned mode, int stable) {
  xe_upload_scan s = {0};
  uint32_t iterations = mode == 0 ? 20000000 : mode == 1 ? 65536 : mode == 2 ? 2048 : 131072;
  uint32_t b;
  uint64_t sum = 0;
  clock_t start = 0;
  result r;
  if (stable && !xe_upload_scan_init(&s, 131072)) abort();
  prepare(mode);
  for (b = 0; b < iterations + 32; ++b) {
    uint32_t first = b & 63, last = mode == 0 ? first + 63 : 131071 - (b & 63) - (mode == 2 ? 3 : 0);
    int all_valid;
    if (b == 32) start = clock();
    all_valid = stable ? stable_all_valid(first, last) : legacy_all_valid(first, last);
    if (all_valid) { ++sum; continue; }
    if (stable) {
      if (!stable_collect(&s, first, last)) abort();
      sum += consume(s.data, s.count);
    } else {
      uint32_t count;
      if (!legacy_collect(first, last, &count)) abort();
      sum += consume(legacy_output, count);
    }
  }
  r.ms = 1000.0 * (clock() - start) / CLOCKS_PER_SEC; r.sum = sum;
  xe_upload_scan_destroy(&s); return r;
}
int main(void) {
  const char *names[] = {"resident 64-page queries", "contiguous invalid", "fragmented partial", "sparse invalid"};
  unsigned mode, i, j;
  uint32_t count;
  xe_upload_scan s = {0};
  prepare(2);
  if (legacy_collect(0, 131071, &count) || count != 65535 || !xe_upload_scan_init(&s, 131072) ||
      !xe_upload_scan_collect(&s, atomic_words, 0, 131071) || s.count != 65536) return 1;
  xe_upload_scan_destroy(&s);
  puts("Legacy capacity rejection reproduced at 65,535 closed runs; C89 accepts all 65,536");
  for (mode = 0; mode < 4; ++mode) {
    double a[3], b[3];
    for (i = 0; i < 3; ++i) {
      result old = run(mode, 0), current = run(mode, 1);
      if (old.sum != current.sum) return 2;
      a[i] = old.ms; b[i] = current.ms;
    }
    for (i = 0; i < 3; ++i) for (j = i + 1; j < 3; ++j) {
      double t;
      if (a[j] < a[i]) { t = a[i]; a[i] = a[j]; a[j] = t; }
      if (b[j] < b[i]) { t = b[i]; b[i] = b[j]; b[j] = t; }
    }
    printf("%s, median3 CPU: defined legacy %.3f ms; C89 atomic %.3f ms\n", names[mode], a[1], b[1]);
  }
  return 0;
}
