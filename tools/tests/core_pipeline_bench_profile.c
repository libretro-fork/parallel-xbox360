/* Profiling-only C89 allocation capture and monotonic batch timing. */
#include "core_pipeline_bench_profile.h"
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <time.h>
#endif
typedef union bench_header { size_t bytes; unsigned char alignment[16]; } bench_header;
static size_t live_bytes, allocations;
static size_t requested_bytes, peak_live_bytes;
void *xe_bench_malloc(size_t size) {
   bench_header *header;
   if (size > (size_t)-1 - sizeof(*header)) return NULL;
   header = (bench_header *)malloc(sizeof(*header) + size);
   if (!header) return NULL;
   header->bytes = size; live_bytes += size; ++allocations;
   requested_bytes += size;
   if (live_bytes > peak_live_bytes) peak_live_bytes = live_bytes;
   return header + 1;
}
void xe_bench_free(void *ptr) {
   bench_header *header;
   if (!ptr) return;
   header = (bench_header *)ptr - 1;
   live_bytes -= header->bytes; free(header);
}
void *xe_bench_calloc(size_t count, size_t size) {
   void *ptr;
   if (size && count > (size_t)-1 / size) return NULL;
   ptr = xe_bench_malloc(count * size);
   if (ptr) memset(ptr, 0, count * size);
   return ptr;
}
void *xe_bench_realloc(void *ptr, size_t size) {
   void *next;
   size_t before;
   if (!ptr) return xe_bench_malloc(size);
   next = xe_bench_malloc(size);
   if (!next) return NULL;
   before = ((bench_header *)ptr - 1)->bytes;
   memcpy(next, ptr, before < size ? before : size);
   xe_bench_free(ptr); return next;
}
size_t xe_bench_live(void) { return live_bytes; }
size_t xe_bench_allocations(void) { return allocations; }
size_t xe_bench_requested(void) { return requested_bytes; }
size_t xe_bench_peak(void) { return peak_live_bytes; }
uint64_t xe_bench_time(void) {
#ifdef _WIN32
   LARGE_INTEGER counter, frequency;
   QueryPerformanceCounter(&counter); QueryPerformanceFrequency(&frequency);
   return (uint64_t)((double)counter.QuadPart * 1000000.0 / (double)frequency.QuadPart);
#else
   struct timespec value;
   clock_gettime(CLOCK_MONOTONIC, &value);
   return (uint64_t)value.tv_sec * UINT64_C(1000000) + (uint64_t)value.tv_nsec / 1000;
#endif
}
static int compare_sample(const void *a, const void *b) {
   uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
   return x < y ? -1 : x > y;
}
void xe_bench_report(const char *label, size_t size, unsigned workers,
      size_t live, uint64_t *samples, unsigned count, size_t jobs) {
   qsort(samples, count, sizeof(*samples), compare_sample);
   printf("%s bytes=%u workers=%u live=%llu median_us=%llu ns/job=%.1f range_us=%llu..%llu\n",
         label, (unsigned)size, workers, (unsigned long long)live,
         (unsigned long long)samples[count / 2],
         (double)samples[count / 2] * 1000.0 / (double)jobs,
         (unsigned long long)samples[0], (unsigned long long)samples[count - 1]);
}
