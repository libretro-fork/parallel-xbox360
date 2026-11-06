#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
static size_t calls, allocated, freed;
static int fail_alloc;
static void *alloc_test(size_t bytes) {
  void *p; ++calls; if (fail_alloc) return NULL;
  p = malloc(bytes); if (p) ++allocated; return p;
}
static void free_test(void *p) { if (p) ++freed; free(p); }
#define XE_DESCRIPTOR_INDEX_ALLOC alloc_test
#define XE_DESCRIPTOR_INDEX_FREE free_test
#include "core_descriptor_indices.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
static int verify_free(const xe_descriptor_indices *s) {
  uint32_t index = s->free_head, count = 0;
  while (index != XE_DESCRIPTOR_INDEX_NONE) {
    CHECK(++count <= s->capacity);
    CHECK(index >= s->reserved && index < s->allocated);
    CHECK(s->next[index] & XE_DESCRIPTOR_INDEX_LIST);
    index = s->next[index] & XE_DESCRIPTOR_INDEX_NONE;
  }
  CHECK(count + s->cached_count == s->free_count);
  for (count = 0; count < s->cached_count; ++count) {
    index = s->cached[count];
    CHECK(index >= s->reserved && index < s->allocated);
    CHECK(s->next[index] & XE_DESCRIPTOR_INDEX_LIST);
  }
  return 0;
}
int main(void) {
  xe_descriptor_indices s = {0};
  xe_descriptor_index_batch current, submitted, rows[4], invalid;
  uint32_t i, index, expected[4] = {6, 5, 4, 7};
  size_t before;
  xe_descriptor_index_batch_clear(&current); xe_descriptor_index_batch_clear(&submitted);
  CHECK(xe_descriptor_index_take(&s) == UINT32_MAX);
  CHECK(!xe_descriptor_indices_init(&s, 0, 0));
  CHECK(!xe_descriptor_indices_init(&s, 2, 3));
  CHECK(!xe_descriptor_indices_init(&s, UINT32_MAX, 0));
  fail_alloc = 1; CHECK(!xe_descriptor_indices_init(&s, 64, 4) && !s.next);
  fail_alloc = 0; CHECK(xe_descriptor_indices_init(&s, 64, 4));
  CHECK(!xe_descriptor_indices_init(&s, 64, 4));
  CHECK(xe_descriptor_index_take(&s) == 4 && xe_descriptor_index_take(&s) == 5);
  CHECK(!xe_descriptor_index_release(&s, 3) && !xe_descriptor_index_release(&s, 6));
  CHECK(xe_descriptor_index_release(&s, 4) && !xe_descriptor_index_release(&s, 4));
  CHECK(xe_descriptor_index_take(&s) == 4);
  CHECK(xe_descriptor_index_release(&s, 5) && xe_descriptor_index_release(&s, 4));
  CHECK(xe_descriptor_index_take_one_use(&s, &current) == 4);
  CHECK(xe_descriptor_index_take_one_use(&s, &current) == 5);
  CHECK(xe_descriptor_index_take_one_use(&s, &current) == 6);
  CHECK(!xe_descriptor_index_release(&s, 4));
  CHECK(!xe_descriptor_index_batch_move(&current, &current));
  CHECK(xe_descriptor_index_batch_move(&submitted, &current) && !current.count && submitted.count == 3);
  CHECK(xe_descriptor_index_take_one_use(&s, &current) == 7);
  CHECK(!xe_descriptor_index_batch_move(&submitted, &current) && current.count == 1);
  CHECK(xe_descriptor_index_batch_recycle(&s, &current));
  CHECK(xe_descriptor_index_batch_recycle(&s, &submitted) && !submitted.count);
  CHECK(xe_descriptor_index_batch_recycle(&s, &submitted));
  for (i = 0; i < 4; ++i) CHECK(xe_descriptor_index_take(&s) == expected[i]);
  invalid.head = invalid.tail = UINT32_MAX; invalid.count = 1;
  CHECK(!xe_descriptor_index_batch_recycle(&s, &invalid));
  while ((index = xe_descriptor_index_take_one_use(&s, &current)) != UINT32_MAX) CHECK(index >= 4 && index < 64);
  before = current.count;
  CHECK(!xe_descriptor_indices_available(&s));
  CHECK(xe_descriptor_index_take_one_use(&s, &current) == UINT32_MAX && current.count == before);
  CHECK(xe_descriptor_index_batch_recycle(&s, &current) && verify_free(&s) == 0);
  xe_descriptor_indices_destroy(&s); xe_descriptor_indices_destroy(&s);
  CHECK(allocated == freed);
  CHECK(xe_descriptor_indices_init(&s, 4, 0));
  CHECK(xe_descriptor_index_take(&s) == 0);
  CHECK(xe_descriptor_index_release(&s, 0) && xe_descriptor_index_take(&s) == 0);
  xe_descriptor_indices_destroy(&s);
  CHECK(xe_descriptor_indices_init(&s, 512, 7));
  for (i = 7; i < 300; ++i) CHECK(xe_descriptor_index_take(&s) == i);
  for (i = 7; i < 300; ++i) CHECK(xe_descriptor_index_release(&s, i));
  CHECK(verify_free(&s) == 0);
  CHECK(!xe_descriptor_index_release(&s, 7) && !xe_descriptor_index_release(&s, 299));
  CHECK(xe_descriptor_index_take_one_use(&s, &current) == 299);
  CHECK(xe_descriptor_index_batch_recycle(&s, &current) && verify_free(&s) == 0);
  for (i = 300; i > 7; --i) CHECK(xe_descriptor_index_take(&s) == i - 1);
  xe_descriptor_indices_destroy(&s);
  CHECK(xe_descriptor_indices_init(&s, 4, 4));
  CHECK(!xe_descriptor_indices_available(&s) && xe_descriptor_index_take(&s) == UINT32_MAX);
  xe_descriptor_indices_destroy(&s);
  CHECK(xe_descriptor_indices_init(&s, 512, 7));
  for (i = 0; i < 4; ++i) xe_descriptor_index_batch_clear(rows + i);
  before = calls;
  for (i = 0; i < 10000; ++i) {
    uint32_t draw;
    CHECK(xe_descriptor_index_batch_recycle(&s, rows + (i % 4)));
    CHECK(xe_descriptor_indices_available(&s) >= 32);
    for (draw = 0; draw < 32; ++draw) CHECK(xe_descriptor_index_take_one_use(&s, &current) != UINT32_MAX);
    CHECK(xe_descriptor_index_batch_move(rows + (i % 4), &current));
  }
  for (i = 0; i < 4; ++i) CHECK(xe_descriptor_index_batch_recycle(&s, rows + i));
  CHECK(calls == before && verify_free(&s) == 0);
  CHECK(s.free_count == s.allocated - s.reserved);
  xe_descriptor_indices_destroy(&s);
  CHECK(xe_descriptor_indices_init(&s, 262144, 8));
  for (i = 8; i < 262144; ++i) CHECK(xe_descriptor_index_take_one_use(&s, &current) == i);
  CHECK(!xe_descriptor_indices_available(&s));
  CHECK(xe_descriptor_index_batch_recycle(&s, &current) && verify_free(&s) == 0);
  for (i = 262144; i > 8; --i) CHECK(xe_descriptor_index_take(&s) == i - 1);
  CHECK(xe_descriptor_index_take(&s) == UINT32_MAX);
  xe_descriptor_indices_destroy(&s);
  CHECK(allocated == freed);
  puts("Descriptor indices C89: 320,000 allocation-free transient indices, full native capacity, exact LIFO, reserved/bounds/double-release guards, batch transfer and OOM pass");
  return 0;
}
