/* Driverless CPU recording cost. The legacy fixed buffer is used only within
 * its valid capacity; no overflow is executed. No native command replay. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
static size_t calls;
static void *bench_alloc(size_t bytes) { ++calls; return malloc(bytes); }
#define XE_COMMAND_STREAM_ALLOC bench_alloc
#define XE_COMMAND_STREAM_FREE free
#include "core_command_stream.h"
typedef struct bench_header { uint32_t command, elements; } bench_header;
typedef struct bench_result { double ms; uint64_t sum; size_t allocations; } bench_result;
#ifdef _MSC_VER
#define BENCH_NOINLINE __declspec(noinline)
#else
#define BENCH_NOINLINE __attribute__((noinline))
#endif
/* Separate consumer forces packet stores to materialize in both versions. */
static BENCH_NOINLINE uint64_t consume(const unsigned char *data, size_t bytes) {
  size_t offset = 0;
  uint64_t sum = 0;
  while (offset < bytes) {
    const bench_header *header = (const bench_header *)(data + offset);
    size_t arguments = (size_t)header->elements * 8;
    const unsigned char *payload = data + offset + sizeof(*header);
    sum += header->command + header->elements + payload[0] + payload[arguments - 1];
    offset += sizeof(*header) + arguments;
  }
  return sum;
}
static bench_result run(unsigned stable) {
  xe_command_stream stream = {0};
  unsigned char *legacy = NULL;
  uint64_t sum = 0;
  unsigned batch, command;
  size_t before, offset = 0;
  clock_t start = 0;
  bench_result result;
  if (stable) {
    if (!xe_command_stream_init(&stream, 8u * 1024u * 1024u)) abort();
  } else {
    legacy = (unsigned char *)malloc(8u * 1024u * 1024u);
    if (!legacy) abort();
  }
  before = calls;
  for (batch = 0; batch < 501000; ++batch) {
    if (batch == 1000) { before = calls; start = clock(); }
    if (stable) xe_command_stream_reset(&stream); else offset = 0;
    for (command = 0; command < 64; ++command) {
      size_t arguments = (command & 7u) * 8u + 8u, bytes;
      uint32_t elements;
      unsigned char *packet, *payload;
      bench_header *header;
      if (stable) {
        if (!xe_command_packet_layout(arguments, sizeof(*header), &elements, &bytes)) abort();
        packet = (unsigned char *)xe_command_stream_append(&stream, bytes);
        if (!packet) abort();
      } else {
        elements = (uint32_t)((arguments + 7) / 8);
        bytes = sizeof(*header) + (size_t)elements * 8;
        packet = legacy + offset; offset += bytes;
      }
      header = (bench_header *)packet; header->command = command; header->elements = elements;
      payload = packet + sizeof(*header);
      payload[0] = (unsigned char)batch; payload[arguments - 1] = (unsigned char)command;
    }
    if (stable) sum += consume(xe_command_page_const_data(stream.first), stream.first->used);
    else sum += consume(legacy, offset);
  }
  result.ms = 1000.0 * (clock() - start) / CLOCKS_PER_SEC;
  result.sum = sum; result.allocations = calls - before;
  xe_command_stream_destroy(&stream); free(legacy); return result;
}
int main(void) {
  double old_times[3], new_times[3];
  unsigned i, j;
  for (i = 0; i < 3; ++i) {
    bench_result a = run(0), b = run(1);
    if (a.sum != b.sum || b.allocations) return 1;
    old_times[i] = a.ms; new_times[i] = b.ms;
  }
  for (i = 0; i < 3; ++i) for (j = i + 1; j < 3; ++j) {
    double t;
    if (old_times[j] < old_times[i]) { t = old_times[i]; old_times[i] = old_times[j]; old_times[j] = t; }
    if (new_times[j] < new_times[i]) { t = new_times[i]; new_times[i] = new_times[j]; new_times[j] = t; }
  }
  printf("Command stream 32M in-capacity packets, median 3 CPU runs: unchecked fixed %.3f ms; checked C89 pages %.3f ms; 0 warmed page allocations\n", old_times[1], new_times[1]);
  return 0;
}
