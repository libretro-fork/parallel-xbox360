#include <stdio.h>
#include <stdlib.h>
static size_t allocations, frees;
static int denied;
static void *index_realloc(void *old, size_t bytes)
{
   void *value;
   if (denied) return NULL;
   value = realloc(old, bytes);
   if (value) ++allocations;
   return value;
}
static void index_free(void *value) { if (value) ++frees; free(value); }
#define XE_SPV_INDEX_REALLOC index_realloc
#define XE_SPV_INDEX_FREE index_free
#include "../../libretro/core_spv_index.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main(void)
{
   xe_spv_index index = {0};
   int records[2048];
   size_t i, round, warm, capacity;
   void **stable;
   void *const *view;
   CHECK(!xe_spv_index_get(&index, 0));
   denied = 1;
   CHECK(!xe_spv_index_set(&index, 0, records) && !index.entries && !index.count);
   denied = 0;
   CHECK(xe_spv_index_reserve(&index, 4096));
   warm = allocations;
   stable = index.entries;
   capacity = index.capacity;
   denied = 1;
   for (round = 0; round < 1000; ++round) {
      xe_spv_index_reset(&index);
      CHECK(!xe_spv_index_get(&index, 2047));
      for (i = 0; i < 2048; ++i) {
         CHECK(xe_spv_index_set(&index, i, &records[i]));
         CHECK(xe_spv_index_get(&index, i) == &records[i]);
      }
      view = xe_spv_index_borrow(&index, 7, 9);
      CHECK(view && view[0] == &records[7] && view[8] == &records[15]);
      CHECK(!xe_spv_index_borrow(&index, (size_t)-1, 9));
      CHECK(!xe_spv_index_borrow(&index, 7, (size_t)-1));
      CHECK(!xe_spv_index_borrow(&index, 2040, 9));
      CHECK(!xe_spv_index_borrow(&index, 7, 0));
      CHECK(index.entries == stable && index.capacity == capacity && allocations == warm);
      CHECK(!xe_spv_index_get(&index, (size_t)-1));
   }
   xe_spv_index_reset(&index);
   CHECK(xe_spv_index_set(&index, 2047, records));
   for (i = 0; i < 2047; ++i) CHECK(!xe_spv_index_get(&index, i));
   CHECK(xe_spv_index_set(&index, 7, &records[7]));
   CHECK(!xe_spv_index_set(&index, 8192, records));
   CHECK(index.entries == stable && index.count == 2048 && index.capacity == capacity);
   CHECK(xe_spv_index_get(&index, 7) == &records[7]);
   CHECK(!xe_spv_index_set(&index, (size_t)-1, records));
   CHECK(!xe_spv_index_reserve(&index, (size_t)-1));
   xe_spv_index_reset(&index);
   CHECK(!index.count && !xe_spv_index_get(&index, 7));
   CHECK(!xe_spv_index_borrow(&index, 7, 1));
   /* Reset keeps backing bytes untouched, but no stale entry is visible. */
   CHECK(index.entries[7] == &records[7]);
   CHECK(xe_spv_index_set(&index, 11, NULL) && !xe_spv_index_get(&index, 7));
   xe_spv_index_close(&index);
   xe_spv_index_close(&index);
   CHECK(allocations == 1 && frees == 1);
   puts("PASS: 2048000 retained C89 ID mappings, sparse holes, stale-ID rejection, overflow and allocation faults");
   return 0;
}
