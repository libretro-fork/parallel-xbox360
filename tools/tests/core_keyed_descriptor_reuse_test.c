#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
static size_t calls, allocated, freed, fail_at;
static void *alloc_test(size_t n) {
  void *p;
  if (++calls == fail_at) return NULL;
  p = malloc(n); if (p) ++allocated; return p;
}
static void free_test(void *p) { if (p) ++freed; free(p); }
#define XE_KEYED_REUSE_ALLOC alloc_test
#define XE_KEYED_REUSE_FREE free_test
#include "core_keyed_descriptor_reuse.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
static int controls(void) {
  xe_keyed_descriptor_reuse s = {0};
  xe_descriptor_reuse_node *n;
  xe_keyed_reuse_entry *entry;
  uint32_t i;
  CHECK(xe_keyed_reuse_warm(&s));
  n = xe_keyed_reuse_acquire(&s, 7, 1); CHECK(n && !n->handle);
  n->handle = 11; CHECK(xe_keyed_reuse_commit(&s));
  n = xe_keyed_reuse_acquire(&s, 7, 2); CHECK(n && !n->handle);
  CHECK(!xe_keyed_reuse_acquire(&s, 9, 2));
  CHECK(!xe_keyed_reuse_reset(&s) && !xe_keyed_reuse_destroy(&s));
  CHECK(!xe_keyed_reuse_commit(&s));
  CHECK(xe_keyed_reuse_collect(&s, 1) == 1 && !s.head);
  xe_keyed_reuse_cancel(&s);
  n = xe_keyed_reuse_acquire(&s, 7, 2); CHECK(n && n->handle == 11);
  CHECK(xe_keyed_reuse_commit(&s));
  n = xe_keyed_reuse_acquire(&s, 7, 2); CHECK(n && !n->handle);
  CHECK(xe_keyed_reuse_collect(&s, 2) == 0);
  xe_keyed_reuse_cancel(&s);
  CHECK(xe_keyed_reuse_collect(&s, 2) == 1);
  n = xe_keyed_reuse_acquire(&s, 7, 4); CHECK(n && n->handle == 11);
  CHECK(xe_keyed_reuse_commit(&s));
  CHECK(!xe_keyed_reuse_acquire(&s, 7, 3));
  CHECK(!xe_keyed_reuse_acquire(&s, 99, 3));
  n = xe_keyed_reuse_acquire(&s, 7, 5); CHECK(n && !n->handle);
  CHECK(xe_keyed_reuse_collect(&s, 4) == 1 && !s.head);
  n->handle = 12; CHECK(xe_keyed_reuse_commit(&s) && s.head);
  n = xe_keyed_reuse_acquire(&s, 0, 5); CHECK(n && !n->handle);
  n->handle = 13; CHECK(xe_keyed_reuse_commit(&s));
  n = xe_keyed_reuse_acquire(&s, UINT32_MAX, 5); CHECK(n && !n->handle);
  n->handle = 14; CHECK(xe_keyed_reuse_commit(&s));
  CHECK(xe_keyed_reuse_collect(&s, 5) == 3 && !s.head);
  n = xe_keyed_reuse_acquire(&s, 7, 6); CHECK(n && n->handle == 12);
  xe_keyed_reuse_cancel(&s);
  CHECK(xe_keyed_reuse_reset(&s));
  n = xe_keyed_reuse_acquire(&s, 0, 1); CHECK(n && !n->handle);
  entry = s.lease_entry; n->handle = 21; CHECK(xe_keyed_reuse_commit(&s));
  CHECK(xe_keyed_reuse_collect(&s, 1) == 1);
  entry->epoch = 1; s.epoch = UINT64_MAX;
  CHECK(xe_keyed_reuse_reset(&s) && s.epoch == 1 && !entry->epoch);
  n = xe_keyed_reuse_acquire(&s, 0, 1); CHECK(n && !n->handle);
  xe_keyed_reuse_cancel(&s);
  n = xe_keyed_reuse_acquire(&s, 0, 2); CHECK(n && !n->handle);
  entry = s.lease_entry; n->handle = 31; CHECK(xe_keyed_reuse_commit(&s));
  for (i = 1; i <= 200; ++i) {
    n = xe_keyed_reuse_acquire(&s, i, 2); CHECK(n && !n->handle);
    n->handle = i + 100; CHECK(xe_keyed_reuse_commit(&s));
  }
  CHECK(xe_keyed_reuse_collect(&s, 1) == 0);
  n = xe_keyed_reuse_acquire(&s, 0, 2); CHECK(n && !n->handle && s.lease_entry == entry);
  CHECK(xe_keyed_reuse_collect(&s, 2) == 0);
  xe_keyed_reuse_cancel(&s);
  CHECK(xe_keyed_reuse_collect(&s, 2) == 201);
  CHECK(xe_keyed_reuse_destroy(&s) && xe_keyed_reuse_destroy(&s));
  return 0;
}
static int workload(xe_keyed_descriptor_reuse *s, int retry) {
  uint64_t frame, id = 0;
  uint32_t key;
  xe_descriptor_reuse_node *n;
  for (frame = 1; frame <= 8; ++frame) {
    xe_keyed_reuse_collect(s, frame > 2 ? frame - 2 : 0);
    for (key = 0; key < 300; ++key) {
      n = xe_keyed_reuse_acquire(s, key * UINT32_C(65537) ^ UINT32_C(0x80000000), frame);
      if (!n && retry) { fail_at = 0; n = xe_keyed_reuse_acquire(s, key * UINT32_C(65537) ^ UINT32_C(0x80000000), frame); }
      CHECK(n);
      if (!n->handle) n->handle = ++id;
      CHECK(xe_keyed_reuse_commit(s));
    }
  }
  CHECK(xe_keyed_reuse_collect(s, UINT64_MAX) == 600 && !s->head);
  return 0;
}
int main(void) {
  xe_keyed_descriptor_reuse s = {0};
  size_t i, before, failed = 0;
  for (i = 1; i <= 4; ++i) {
    fail_at = calls + i;
    CHECK(!xe_keyed_reuse_warm(&s) && !s.epoch);
    CHECK(xe_keyed_reuse_reset(&s)); fail_at = 0;
    CHECK(xe_keyed_reuse_warm(&s) && xe_keyed_reuse_destroy(&s));
  }
  CHECK(controls() == 0);
  for (i = 1; i <= 60; ++i) {
    fail_at = calls + i; before = fail_at;
    CHECK(workload(&s, 1) == 0);
    if (!fail_at) ++failed;
    else CHECK(calls < before);
    fail_at = 0; CHECK(xe_keyed_reuse_destroy(&s));
  }
  CHECK(failed >= 50);
  CHECK(workload(&s, 0) == 0);
  before = calls;
  for (i = 0; i < 100; ++i) {
    CHECK(xe_keyed_reuse_reset(&s)); CHECK(workload(&s, 0) == 0); CHECK(calls == before);
  }
  CHECK(xe_keyed_reuse_destroy(&s) && allocated == freed);
  printf("Keyed reuse C89: warm/growth failures (%lu), epoch wrap, lease/collection pinning, full-width keys and 240,000 warmed leases pass\n", (unsigned long)failed);
  return 0;
}
