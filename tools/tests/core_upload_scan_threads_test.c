/* Real rthreads concurrency: one serialized writer, an unlocked hint reader,
 * and a collector taking the same writer lock. No GPU/data publication claim. */
#include <stdio.h>
#include <string.h>
#include <rthreads/rthreads.h>
#include "core_upload_scan.h"
struct state {
  xe_page_valid_word words[2];
  uint64_t source[2];
  xe_upload_scan scan;
  slock_t *lock;
  retro_atomic_int_t go, ready, done, error;
  uint64_t reads, collects;
};
static void ready(struct state *s) {
  retro_atomic_inc_int(&s->ready);
  while (!retro_atomic_load_acquire_int(&s->go)) {}
}
static void writer(void *opaque) {
  struct state *s = (struct state *)opaque;
  uint32_t i;
  ready(s);
  for (i = 0; i < 50000; ++i) {
    uint64_t value = (i & 3) == 0 ? 0 : (i & 3) == 1 ? UINT64_MAX :
        (i & 3) == 2 ? UINT64_C(0xaaaaaaaaaaaaaaaa) : UINT64_C(0x5555555555555555);
    slock_lock(s->lock);
    xe_page_valid_store(s->words, UINT64_C(0xaaaaaaaaaaaaaaaa));
    xe_page_valid_or(s->words, UINT64_C(0x5555555555555555));
    xe_page_valid_and(s->words, UINT64_C(0xaaaaaaaaaaaaaaaa));
    s->source[0] = s->source[1] = value;
    xe_page_valid_copy(s->words, s->source, 2, 0, 64);
    xe_page_valid_clear(s->words, 2);
    xe_page_valid_copy(s->words, s->source, 2, 0, 2);
    slock_unlock(s->lock);
  }
  retro_atomic_store_release_int(&s->done, 1);
}
static void reader(void *opaque) {
  struct state *s = (struct state *)opaque;
  ready(s);
  do {
    uint32_t i;
    for (i = 0; i < 2; ++i) {
      uint64_t v = xe_page_valid_load(s->words + i);
      if (v != 0 && v != UINT64_MAX && v != UINT64_C(0xaaaaaaaaaaaaaaaa) &&
          v != UINT64_C(0x5555555555555555)) retro_atomic_store_relaxed_int(&s->error, 1);
    }
    /* Atomic hints may change between words; never assert a whole-array epoch. */
    (void)xe_upload_all_valid(s->words, 128, 1, 126);
    ++s->reads;
  } while (s->reads < 50000 || !retro_atomic_load_acquire_int(&s->done));
}
static void collector(void *opaque) {
  struct state *s = (struct state *)opaque;
  uint32_t iteration;
  ready(s);
  for (iteration = 0; iteration < 20000; ++iteration) {
    uint32_t page, range = 0, start = UINT32_MAX;
    slock_lock(s->lock);
    if (!xe_upload_scan_collect(&s->scan, s->words, 0, 127)) retro_atomic_store_relaxed_int(&s->error, 1);
    for (page = 0; page <= 128; ++page) {
      int valid = page == 128 || (s->source[page >> 6] & (UINT64_C(1) << (page & 63))) != 0;
      if (!valid && start == UINT32_MAX) start = page;
      if (valid && start != UINT32_MAX) {
        if (range >= s->scan.count || s->scan.data[range].first != start ||
            s->scan.data[range].second != page - start) retro_atomic_store_relaxed_int(&s->error, 1);
        ++range; start = UINT32_MAX;
      }
    }
    if (range != s->scan.count) retro_atomic_store_relaxed_int(&s->error, 1);
    slock_unlock(s->lock); ++s->collects;
  }
}
int main(void) {
  struct state s;
  sthread_t *a, *b, *c;
  memset(&s, 0, sizeof(s));
  s.lock = slock_new();
  if (!s.lock) return 1;
  if (!xe_upload_scan_init(&s.scan, 128)) { slock_free(s.lock); return 1; }
  a = sthread_create(writer, &s); b = sthread_create(reader, &s); c = sthread_create(collector, &s);
  if (!a || !b || !c) {
    retro_atomic_store_release_int(&s.done, 1);
    retro_atomic_store_release_int(&s.go, 1);
    if (a) sthread_join(a);
    if (b) sthread_join(b);
    if (c) sthread_join(c);
    slock_free(s.lock); xe_upload_scan_destroy(&s.scan); return 2;
  }
  while (retro_atomic_load_acquire_int(&s.ready) != 3) {}
  retro_atomic_store_release_int(&s.go, 1);
  sthread_join(a); sthread_join(b); sthread_join(c);
  slock_free(s.lock); xe_upload_scan_destroy(&s.scan);
  if (retro_atomic_load_relaxed_int(&s.error)) return 3;
  printf("Upload scan rthreads: 50,000 serialized updates, %lu unlocked reads and %lu exact locked collections pass\n",
      (unsigned long)s.reads, (unsigned long)s.collects);
  return 0;
}
