#include "core_pointer_pool.h"
#include <rthreads/rthreads.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) abort(); } while (0)
#define WORKERS 6
#define ITERATIONS 50000
struct item { retro_atomic_int_t owned; unsigned count; unsigned check; };
static struct item items[XE_POINTER_POOL_CAPACITY + 1];
static xe_pointer_pool pool;
static retro_atomic_int_t ready, start;
static void worker(void *unused) {
   unsigned i;
   struct item *item;
   (void)unused;
   retro_atomic_fetch_add_int(&ready, 1);
   while (!retro_atomic_load_acquire_int(&start)) {}
   for (i = 0; i < ITERATIONS; ++i) {
      do { item = (struct item *)xe_pointer_pool_take(&pool); } while (!item);
      CHECK(retro_atomic_cas_int(&item->owned, 0, 1));
      CHECK(item->check == (item->count ^ 0xabcd1234u));
      ++item->count;
      item->check = item->count ^ 0xabcd1234u;
      retro_atomic_store_release_int(&item->owned, 0);
      while (!xe_pointer_pool_put(&pool, item)) {}
   }
}
int main(void) {
   unsigned i, sum = 0, count = 0;
   unsigned seen[XE_POINTER_POOL_CAPACITY] = {0};
   struct item *item;
   sthread_t *threads[WORKERS];
   xe_pointer_pool_init(&pool);
   CHECK(!xe_pointer_pool_take(&pool) && !xe_pointer_pool_put(&pool, NULL));
   for (i = 0; i < XE_POINTER_POOL_CAPACITY; ++i) {
      items[i].check = 0xabcd1234u;
      CHECK(xe_pointer_pool_put(&pool, &items[i]));
   }
   CHECK(!xe_pointer_pool_put(&pool, &items[XE_POINTER_POOL_CAPACITY]));
   for (i = 0; i < WORKERS; ++i) {
      threads[i] = sthread_create(worker, NULL); CHECK(threads[i]);
   }
   while (retro_atomic_load_acquire_int(&ready) != WORKERS) {}
   retro_atomic_store_release_int(&start, 1);
   for (i = 0; i < WORKERS; ++i) sthread_join(threads[i]);
   while ((item = (struct item *)xe_pointer_pool_take(&pool)) != NULL) {
      CHECK(item >= items && item < items + XE_POINTER_POOL_CAPACITY);
      CHECK(!seen[item - items]++ && !retro_atomic_load_acquire_int(&item->owned));
      CHECK(item->check == (item->count ^ 0xabcd1234u));
      sum += item->count; ++count;
   }
   CHECK(count == XE_POINTER_POOL_CAPACITY && sum == WORKERS * ITERATIONS);
   puts("PASS: bounded pointer ownership, overflow, six consumers/producers and payload visibility");
   return 0;
}
