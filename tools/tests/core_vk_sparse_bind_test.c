#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
static size_t calls, allocs, frees, fail_at;
static void *test_alloc(size_t n) {
  void *p;
  if (++calls == fail_at) return NULL;
  p = malloc(n); if (p) ++allocs; return p;
}
static void *test_realloc(void *old, size_t n) {
  void *p;
  int had_old = old != NULL;
  if (++calls == fail_at) return NULL;
  p = realloc(old, n); if (p && !had_old) ++allocs; return p;
}
static void test_free(void *p) { if (p) ++frees; free(p); }
#define XE_SPARSE_ALLOC test_alloc
#define XE_SPARSE_REALLOC test_realloc
#define XE_SPARSE_FREE test_free
#include "core_vk_sparse_bind.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
#define BUFFER(x) ((VkBuffer)(uintptr_t)(x))
#define MEMORY(x) ((VkDeviceMemory)(uintptr_t)(x))
static int verify(const xe_sparse_bind_queue *q, size_t n) {
  size_t i;
  const VkSparseMemoryBind *b;
  CHECK(q->count == n);
  for (i = 0; i < n; ++i) {
    CHECK(q->infos[i].buffer == BUFFER(i + 1));
    CHECK(q->infos[i].bindCount == 1);
    b = q->infos[i].pBinds;
    CHECK(b->resourceOffset == (VkDeviceSize)i * UINT64_C(65536));
    CHECK(b->size == UINT64_C(65536) + i);
    CHECK(b->memory == MEMORY(i + 100));
    CHECK(!b->memoryOffset && !b->flags);
  }
  return 0;
}
static int append(xe_sparse_bind_queue *q, size_t i) {
  return xe_sparse_bind_push(q, BUFFER(i + 1), (VkDeviceSize)i * UINT64_C(65536),
      UINT64_C(65536) + i, MEMORY(i + 100), (VkPipelineStageFlags)(1u << (i % 16)));
}
int main(void) {
  xe_sparse_bind_queue q = {0};
  const VkSparseMemoryBind *first;
  size_t i, r, before;
  fail_at = calls + 1;
  CHECK(!xe_sparse_bind_warm(&q) && !q.infos && !q.pages);
  fail_at = calls + 2;
  CHECK(!xe_sparse_bind_warm(&q) && q.infos && !q.pages && !q.count);
  fail_at = 0;
  CHECK(xe_sparse_bind_warm(&q));
  for (i = 0; i < 128; ++i) CHECK(append(&q, i));
  first = q.infos[0].pBinds;
  CHECK(verify(&q, 128) == 0 && q.wait_stages == 65535);
  fail_at = calls + 1;
  CHECK(!append(&q, 128));
  CHECK(verify(&q, 128) == 0 && q.next_bind == 128);
  fail_at = calls + 2;
  CHECK(!append(&q, 128));
  CHECK(verify(&q, 128) == 0 && q.next_bind == 128);
  fail_at = 0;
  for (i = 128; i < 769; ++i) CHECK(append(&q, i));
  CHECK(verify(&q, 769) == 0 && first == q.infos[0].pBinds);
  CHECK(!xe_sparse_bind_reserve(&q, (size_t)-1));
  CHECK(verify(&q, 769) == 0);
  before = calls;
  for (r = 0; r < 1000; ++r) {
    xe_sparse_bind_clear(&q);
    CHECK(!q.count && !q.wait_stages);
    for (i = 0; i < 513; ++i) CHECK(append(&q, i));
    CHECK(verify(&q, 513) == 0 && first == q.infos[0].pBinds);
  }
  CHECK(calls == before);
  xe_sparse_bind_destroy(&q);
  CHECK(!q.pages && !q.infos && !q.count && !q.wait_stages);
  xe_sparse_bind_destroy(&q);
  CHECK(allocs == frees);
  CHECK(append(&q, 0));
  CHECK(verify(&q, 1) == 0);
  xe_sparse_bind_destroy(&q);
  CHECK(allocs == frees);
  puts("Sparse binds: 513,000 final-record checks, stable borrowed pages, failure rollback and no allocations after warm high-water pass");
  return 0;
}
