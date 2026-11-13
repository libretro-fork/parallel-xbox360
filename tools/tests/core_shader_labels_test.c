/* Owned C89 label bitmap against an independent byte-set oracle. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static size_t calls, releases, bytes;
static int fail;
static void *allocate(size_t count, size_t stride)
{
   ++calls;
   if (fail) return NULL;
   bytes += count * stride;
   return calloc(count, stride);
}
static void release(void *p) { if (p) ++releases; free(p); }
#define XE_SHADER_LABELS_CALLOC allocate
#define XE_SHADER_LABELS_FREE release
#include "libretro/core_shader_labels.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"shader labels line %u\n",(unsigned)__LINE__); exit(1); } } while (0)
static unsigned char oracle[8256];
static uint32_t random_state = UINT32_C(0xc4870a35);
static uint32_t next_random(void)
{
   random_state ^= random_state << 13;
   random_state ^= random_state >> 17;
   random_state ^= random_state << 5;
   return random_state;
}
static void validate(const xe_shader_labels *labels)
{
   size_t i, next = xe_shader_labels_next(labels, 0), count = 0;
   for (i = 0; i < sizeof(oracle); ++i) {
      CHECK(xe_shader_labels_contains(labels, (uint32_t)i) == (oracle[i] != 0));
      if (!oracle[i]) continue;
      CHECK(next == i);
      next = xe_shader_labels_next(labels, next + 1);
      ++count;
   }
   CHECK(next == (size_t)-1 && labels->count == count);
}
int main(void)
{
   xe_shader_labels labels = {{0},NULL,0,0}, other = {{0},NULL,0,0}, snapshot;
   uint64_t low[4], *owner;
   size_t i, label, before, trial;
   CHECK(!xe_shader_labels_init(NULL, 0));
   CHECK(!xe_shader_labels_init(&labels, (size_t)-1));
   CHECK(!labels.words && !calls);
   CHECK(xe_shader_labels_init(&labels, 4096));
   CHECK(!xe_shader_labels_init(&labels, 1));
   CHECK(!calls && !labels.count && xe_shader_labels_next(&labels, 0) == (size_t)-1);
   for (i = 0; i < 256; ++i) {
      CHECK(xe_shader_labels_insert(&labels, (uint32_t)i) == 1);
      CHECK(xe_shader_labels_insert(&labels, (uint32_t)i) == 0);
      oracle[i] = 1;
   }
   CHECK(!calls && !labels.high);
   memcpy(low, labels.low, sizeof(low)); snapshot = labels;
   fail = 1;
   CHECK(xe_shader_labels_insert(&labels, 256) == -1);
   CHECK(!memcmp(&snapshot, &labels, sizeof(labels)));
   CHECK(!xe_shader_labels_contains(&labels, 256));
   fail = 0; before = calls;
   CHECK(xe_shader_labels_insert(&labels, 256) == 1);
   oracle[256] = 1; owner = labels.high;
   CHECK(calls == before + 1 && owner && bytes == 1000);
   CHECK(!memcmp(low, labels.low, sizeof(low)));
   CHECK(xe_shader_labels_insert(&labels, 8256) == -1);
   CHECK(!xe_shader_labels_contains(&labels, UINT32_MAX));
   CHECK(xe_shader_labels_next(&labels, (size_t)-1) == (size_t)-1);
   CHECK(xe_shader_labels_next(&labels, SIZE_MAX - 1) == (size_t)-1);
   for (trial = 0; trial < 50000; ++trial) {
      label = next_random() % 8193;
      CHECK(xe_shader_labels_insert(&labels, (uint32_t)label) == !oracle[label]);
      oracle[label] = 1;
      if (!(trial % 1000)) validate(&labels);
   }
   validate(&labels);
   CHECK(labels.high == owner && calls == before + 1);
   CHECK(xe_shader_labels_init(&other, 4096));
   for (i = 0; i < sizeof(oracle); ++i)
      if (oracle[i]) CHECK(xe_shader_labels_insert(&other, (uint32_t)i) == 1);
   CHECK(xe_shader_labels_equal(&labels, &other));
   CHECK(!xe_shader_labels_equal(NULL, &other));
   CHECK(!xe_shader_labels_equal(&labels, NULL));
   xe_shader_labels_close(&labels); xe_shader_labels_close(&labels);
   CHECK(!xe_shader_labels_equal(&labels, &other));
   xe_shader_labels_close(&other);
   CHECK(releases == 2);
   /* Empty owners with different configured ranges have equal sets. */
   CHECK(xe_shader_labels_init(&labels, 4096));
   CHECK(xe_shader_labels_init(&other, 0));
   CHECK(xe_shader_labels_equal(&labels, &other));
   xe_shader_labels_close(&labels); xe_shader_labels_close(&other);
   CHECK(releases == 2);
   CHECK(xe_shader_labels_init(&labels, 10000));
   CHECK(xe_shader_labels_insert(&labels, 20000) == 1);
   CHECK(xe_shader_labels_contains(&labels, 20000));
   CHECK(xe_shader_labels_next(&labels, 0) == 20000);
   CHECK(xe_shader_labels_next(&labels, 20001) == (size_t)-1);
   xe_shader_labels_close(&labels);
   CHECK(releases == 3);
   /* A sparse inline-only owner must not scan its unallocated high universe. */
   CHECK(xe_shader_labels_init(&labels, SIZE_MAX / 4));
   CHECK(xe_shader_labels_insert(&labels, 0) == 1);
   CHECK(xe_shader_labels_next(&labels, 0) == 0);
   CHECK(xe_shader_labels_next(&labels, 1) == SIZE_MAX);
   CHECK(!labels.high);
   xe_shader_labels_close(&labels);
   CHECK(releases == 3);
   puts("PASS: 50000 insertions vs byte-set oracle, ordered views, membership, duplicates, one bounded allocation, no inline promotion copy, failure preservation and close/reopen");
   return 0;
}
