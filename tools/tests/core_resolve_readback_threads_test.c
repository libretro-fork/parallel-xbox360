/* Real rthreads: concurrent atomic key writer, unlocked key reader, one fault
 * record owner. Snapshot borrows must not change when keys change. */
#include <stdio.h>
#include <string.h>
#include <rthreads/rthreads.h>
#include "core_resolve_readback.h"
struct state {
  xe_resolve_readback pages;
  retro_atomic_int_t go, ready, done, error;
  uint64_t reads, snapshots;
};
#define KEY_A UINT64_C(0xaaaaaaaa55555555)
#define KEY_B UINT64_C(0x55555555aaaaaaaa)
static void ready(struct state *s) {
  retro_atomic_inc_int(&s->ready);
  while (!retro_atomic_load_acquire_int(&s->go)) {}
}
static void writer(void *opaque) {
  struct state *s = (struct state *)opaque;
  uint32_t i, page;
  ready(s);
  for (i = 0; i < 50000; ++i) for (page = 0; page < 128; ++page)
    xe_resolve_owner_set(&s->pages, page << 12, 4096, ((page ^ i) & 1) ? KEY_A : KEY_B);
  retro_atomic_store_release_int(&s->done, 1);
}
static void reader(void *opaque) {
  struct state *s = (struct state *)opaque;
  ready(s);
  do {
    uint32_t page;
    for (page = 0; page < 128; ++page) {
      uint64_t key = xe_resolve_owner_load(&s->pages, page);
      if (key != 0 && key != KEY_A && key != KEY_B) retro_atomic_store_relaxed_int(&s->error, 1);
    }
    ++s->reads;
  } while (s->reads < 50000 || !retro_atomic_load_acquire_int(&s->done));
}
static uint64_t consume(struct state *s, const xe_resolve_copy_plan *plan) {
  xe_resolve_copy_cursor cursor = xe_resolve_cursor_make(plan);
  xe_memexport_run range;
  uint32_t count = 0, bytes = 0, end = 0;
  uint64_t sum = 0;
  while (xe_resolve_next(&cursor, &range)) {
    if (!range.size || range.base < end || range.base < 31 || range.base > 128 * 4096 - 67 ||
        range.size > 128 * 4096 - 67 - range.base) retro_atomic_store_relaxed_int(&s->error, 1);
    bytes += range.size; ++count; end = range.base + range.size;
    sum += range.base + (uint64_t)range.size * 3;
  }
  if (count != plan->count || bytes != plan->bytes || count > 64) retro_atomic_store_relaxed_int(&s->error, 1);
  return sum;
}
static void collector(void *opaque) {
  struct state *s = (struct state *)opaque;
  uint32_t iteration;
  ready(s);
  for (iteration = 0; iteration < 20000; ++iteration) {
    xe_resolve_copy_plan plan;
    if (xe_resolve_snapshot_make(&s->pages, KEY_A, 31, 128 * 4096 - 98, 1, 1, 1, &plan)) {
      uint64_t first = consume(s, &plan);
      uint32_t i;
      for (i = 0; i < 64; ++i) (void)xe_resolve_owner_load(&s->pages, i);
      if (first != consume(s, &plan)) retro_atomic_store_relaxed_int(&s->error, 1);
    } else if (plan.count || plan.bytes) retro_atomic_store_relaxed_int(&s->error, 1);
    ++s->snapshots;
  }
}
int main(void) {
  struct state s;
  sthread_t *a, *b, *c;
  int result;
  memset(&s, 0, sizeof(s));
  if (!xe_resolve_readback_init(&s.pages, 0)) return 1;
  a = sthread_create(writer, &s); b = sthread_create(reader, &s); c = sthread_create(collector, &s);
  if (!a || !b || !c) {
    retro_atomic_store_release_int(&s.done, 1); retro_atomic_store_release_int(&s.go, 1);
    if (a) sthread_join(a);
    if (b) sthread_join(b);
    if (c) sthread_join(c);
    xe_resolve_readback_destroy(&s.pages); return 2;
  }
  while (retro_atomic_load_acquire_int(&s.ready) != 3) {}
  retro_atomic_store_release_int(&s.go, 1);
  sthread_join(a); sthread_join(b); sthread_join(c);
  result = retro_atomic_load_relaxed_int(&s.error);
  xe_resolve_readback_destroy(&s.pages);
  printf("Resolve concurrency: 6.4M key stores, %lu reader passes, %lu stable record leases, error=%d\n", (unsigned long)s.reads, (unsigned long)s.snapshots, result);
  return result;
}
