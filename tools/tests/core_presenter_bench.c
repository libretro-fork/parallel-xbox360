/* C89, no driver/GPU work: warm synchronous callback/publication batches. */
#include "core_pipeline_bench_profile.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) abort(); } while (0)
void *xe_presenter_bench_new(void);
void xe_presenter_bench_free(void *p);
int xe_presenter_bench_run(void *p, unsigned words, unsigned value);

int main(void) {
  void *presenter;
  unsigned words, sample, i;
  size_t calls, bytes, base_calls, base_bytes;
  uint64_t start, times[7];
  presenter = xe_presenter_bench_new();
  CHECK(presenter);
  for (words = 4; words <= 5; ++words) {
    for (i = 0; i < 1000; ++i)
      CHECK(xe_presenter_bench_run(presenter, words, i));
    base_calls = xe_bench_allocations();
    base_bytes = xe_bench_requested();
    for (sample = 0; sample < 7; ++sample) {
      start = xe_bench_time();
      for (i = 0; i < 100000; ++i)
        CHECK(xe_presenter_bench_run(presenter, words, i));
      times[sample] = xe_bench_time() - start;
    }
    calls = xe_bench_allocations() - base_calls;
    bytes = xe_bench_requested() - base_bytes;
    printf("PROFILE capture_bytes=%u refreshes=700000 cpp_calls=%lu cpp_requested=%lu\n",
        words * (unsigned)sizeof(void*), (unsigned long)calls, (unsigned long)bytes);
    xe_bench_report("presenter", words * sizeof(void*), 0,
        xe_bench_live(), times, 7, 100000);
  }
  xe_presenter_bench_free(presenter);
  CHECK(xe_bench_live() == 0);
  return 0;
}
