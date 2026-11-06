#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
static size_t calls, allocated, freed, fail_at;
static void *alloc_test(size_t bytes) {
  void *p;
  if (++calls == fail_at) return NULL;
  p = malloc(bytes); if (p) ++allocated; return p;
}
static void free_test(void *p) { if (p) ++freed; free(p); }
#define XE_POOL_SEARCH_ALLOC alloc_test
#define XE_POOL_SEARCH_FREE free_test
#include "core_vk_pool_search.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
static xe_vk_pool_search_node *nodes[2048];
static uint32_t random_state = 15713;
static uint32_t random_next(void) {
  random_state ^= random_state << 13; random_state ^= random_state >> 17; random_state ^= random_state << 5;
  return random_state;
}
static int verify(xe_vk_pool_search *s, size_t expected, int ties) {
  xe_vk_pool_search_node *n, *previous = NULL, *p;
  size_t count = 0, depth;
  CHECK(!s->root || !s->root->parent);
  for (n = s->maximum; n; n = xe_vk_pool_search_previous(n)) {
    CHECK(++count <= expected);
    if (previous) {
      CHECK(previous->key >= n->key);
      if (ties && previous->key == n->key) CHECK((uintptr_t)previous->pool > (uintptr_t)n->pool);
    }
    if (n->left) CHECK(n->left->parent == n && n->left->key <= n->key && n->left->priority >= n->priority);
    if (n->right) CHECK(n->right->parent == n && n->right->key >= n->key && n->right->priority >= n->priority);
    depth = 0; for (p = n; p; p = p->parent) CHECK(++depth <= 128);
    previous = n;
  }
  CHECK(count == expected); return 0;
}
int main(void) {
  xe_vk_pool_search s = {0};
  xe_vk_pool_search_node *n;
  size_t i, epoch, remaining, index, before = 0;
  uint32_t key;
  fail_at = 1; CHECK(!xe_vk_pool_search_warm(&s)); fail_at = 0;
  CHECK(xe_vk_pool_search_warm(&s));
  n = xe_vk_pool_search_acquire(&s); CHECK(n && !xe_vk_pool_search_acquire(&s));
  CHECK(!xe_vk_pool_search_reset(&s) && !xe_vk_pool_search_destroy(&s));
  xe_vk_pool_search_cancel(&s);
  for (i = 0; i < 32; ++i) {
    n = xe_vk_pool_search_acquire(&s); CHECK(n); n->pool = (VkDescriptorPool)(uintptr_t)(i + 1);
    CHECK(xe_vk_pool_search_commit(&s) == n); xe_vk_pool_search_insert(&s, n, (uint32_t)i);
  }
  fail_at = calls + 1; CHECK(!xe_vk_pool_search_acquire(&s)); fail_at = 0;
  CHECK(verify(&s, 32, 1) == 0);
  CHECK(xe_vk_pool_search_reset(&s));
  for (epoch = 0; epoch < 100; ++epoch) {
    for (i = 0; i < 2048; ++i) {
      n = xe_vk_pool_search_acquire(&s); CHECK(n);
      n->pool = (VkDescriptorPool)(uintptr_t)(i + 1); nodes[i] = n;
      switch (epoch % 4) {
        case 0: key = (uint32_t)i; break;
        case 1: key = 2048 - (uint32_t)i; break;
        case 2: key = 7; break;
        default: key = random_next() & 127; break;
      }
      CHECK(xe_vk_pool_search_commit(&s) == n); xe_vk_pool_search_insert(&s, n, key);
    }
    CHECK(verify(&s, 2048, 1) == 0);
    if (!epoch) before = calls;
    for (remaining = 2048; remaining; --remaining) {
      index = random_next() % remaining; n = nodes[index]; nodes[index] = nodes[remaining - 1];
      xe_vk_pool_search_erase(&s, n); xe_vk_pool_search_release(&s, n);
      if (!(remaining % 64)) CHECK(verify(&s, remaining - 1, 1) == 0);
    }
    CHECK(!s.root && !s.maximum && calls == before);
    CHECK(xe_vk_pool_search_reset(&s));
  }
  CHECK(xe_vk_pool_search_destroy(&s) && xe_vk_pool_search_destroy(&s));
  CHECK(allocated == freed);
  puts("Pool search C89: 204,800 nodes inserted/erased, ordered/tied/random keys, parent/heap invariants, OOM rollback and warmed allocation reuse pass");
  return 0;
}
