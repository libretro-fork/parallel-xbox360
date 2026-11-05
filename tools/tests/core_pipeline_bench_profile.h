#ifndef XE_PIPELINE_BENCH_PROFILE_H
#define XE_PIPELINE_BENCH_PROFILE_H
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#ifdef __cplusplus
extern "C" {
#endif
void *xe_bench_malloc(size_t size);
void *xe_bench_calloc(size_t count, size_t size);
void *xe_bench_realloc(void *ptr, size_t size);
void xe_bench_free(void *ptr);
size_t xe_bench_live(void);
size_t xe_bench_allocations(void);
size_t xe_bench_requested(void);
size_t xe_bench_peak(void);
uint64_t xe_bench_time(void);
void xe_bench_report(const char *label, size_t size, unsigned workers,
      size_t live, uint64_t *samples, unsigned count, size_t jobs);
#ifdef __cplusplus
}
#endif
#ifdef XE_CAPTURE_ALLOCATIONS
#define malloc xe_bench_malloc
#define calloc xe_bench_calloc
#define realloc xe_bench_realloc
#define free xe_bench_free
#endif
#endif
