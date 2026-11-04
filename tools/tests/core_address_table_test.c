/* Also reproduce Vulkan-Hpp's removal of the Windows fence macro. */
#ifdef _WIN32
#include <windows.h>
#undef MemoryBarrier
#endif
#include "core_address_table.h"
#include <rthreads/rthreads.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) abort(); } while (0)
#define KEYS 128
#define WORKERS 6
typedef char raw_slot_size_check[sizeof(retro_atomic_64_t) == sizeof(uint64_t) ? 1 : -1];
static retro_atomic_64_t slots[KEYS];
static size_t indices[WORKERS][KEYS];
static unsigned ids[WORKERS];
static retro_atomic_int_t ready, start;
static retro_atomic_int_t published[KEYS];
static uint64_t key(unsigned i) {
   return i ? UINT64_C(0x7fffabcd00000000) + i : 0;
}
static void worker(void *opaque) {
   unsigned id = *(unsigned *)opaque, i, j, k;
   size_t slot;
   retro_atomic_fetch_add_int(&ready, 1);
   while (!retro_atomic_load_acquire_int(&start)) {}
   for (k = 0; k < 1000; ++k) {
      for (i = 0; i < KEYS; ++i) {
         j = (i + id * 17) % KEYS;
         {
            int inserted = xe_address_table_insert(slots, KEYS, key(j), &slot);
            CHECK(inserted >= 0);
            if (inserted) retro_atomic_fetch_add_int(&published[j], 1);
         }
         CHECK(slot < KEYS);
         CHECK((uint64_t)retro_atomic_load_relaxed_64(&slots[slot]) == key(j));
         if (!k) indices[id][j] = slot;
         else CHECK(indices[id][j] == slot);
      }
   }
}
int main(void) {
   unsigned i, j, seen[KEYS] = {0};
   sthread_t *threads[WORKERS];
   xe_address_table_init(slots, KEYS);
   retro_atomic_thread_fence_acquire();
   retro_atomic_thread_fence_release();
   retro_atomic_thread_fence_seq_cst();
   for (i = 0; i < WORKERS; ++i) {
      ids[i] = i; threads[i] = sthread_create(worker, &ids[i]); CHECK(threads[i]);
   }
   while (retro_atomic_load_acquire_int(&ready) != WORKERS) {}
   retro_atomic_store_release_int(&start, 1);
   for (i = 0; i < WORKERS; ++i) sthread_join(threads[i]);
   for (j = 0; j < KEYS; ++j) {
      CHECK(retro_atomic_load_acquire_int(&published[j]) == 1);
      CHECK(!seen[indices[0][j]]++);
      for (i = 1; i < WORKERS; ++i) CHECK(indices[i][j] == indices[0][j]);
   }
   CHECK(xe_address_table_intern(slots, KEYS, UINT64_C(0x1234)) == KEYS);
   CHECK(xe_address_table_intern(slots, KEYS, UINT64_MAX) == KEYS);
   puts("PASS: concurrent address deduplication, immutable raw slots, exhaustion and fence include order");
   return 0;
}
