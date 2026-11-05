/* Fixed shader-ID scratch must retain its storage and reject overflow. */
#include "../../libretro/core_spv_words.h"
#include <stdio.h>
#include <stdlib.h>
static size_t allocations, frees;
static int fail;
static void *scratch_alloc(size_t bytes)
{
   void *value;
   if (fail) return NULL;
   value = malloc(bytes);
   if (value) ++allocations;
   return value;
}
static void scratch_free(void *value)
{
   if (value) ++frees;
   free(value);
}
#define XE_SPV_SCRATCH_ALLOC scratch_alloc
#define XE_SPV_SCRATCH_FREE scratch_free
#include "../../libretro/core_spv_scratch.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
static int check_retained_scratch(void)
{
   xe_spv_scratch scratch = {0};
   uint32_t expected[4096], *stable;
   size_t i, round, count, before;
   for (i = 0; i < 64; ++i) {
      CHECK(xe_spv_scratch_push(&scratch, (uint32_t)i));
      expected[i] = (uint32_t)i;
   }
   CHECK(!allocations && xe_spv_scratch_view(&scratch).words == scratch.local);
   stable = xe_spv_scratch_data(&scratch);
   fail = 1;
   CHECK(!xe_spv_scratch_push(&scratch, 64));
   CHECK(!xe_spv_scratch_reserve(&scratch, (size_t)-1));
   CHECK(scratch.count == 64 && xe_spv_scratch_data(&scratch) == stable);
   for (i = 0; i < 64; ++i) CHECK(stable[i] == expected[i]);
   fail = 0;
   CHECK(xe_spv_scratch_resize(&scratch, 4096, 77));
   for (i = 64; i < 4096; ++i) expected[i] = 77;
   stable = xe_spv_scratch_data(&scratch);
   before = allocations;
   count = 4096;
   for (round = 0; round < 10000; ++round) {
      size_t next = (round * 73) % 4096;
      uint32_t fill = (uint32_t)(round * 19);
      CHECK(xe_spv_scratch_resize(&scratch, next, fill));
      for (i = count; i < next; ++i) expected[i] = fill;
      count = next;
      CHECK(scratch.count == count && allocations == before);
      CHECK(xe_spv_scratch_view(&scratch).words == stable);
      for (i = 0; i < count; ++i) CHECK(stable[i] == expected[i]);
   }
   CHECK(!xe_spv_scratch_fill(&scratch, (size_t)-1, 0));
   CHECK(scratch.count == count && allocations == before);
   scratch.count = 0;
   CHECK(xe_spv_scratch_fill(&scratch, 28, 99));
   for (i = 0; i < 28; ++i) CHECK(stable[i] == 99);
   xe_spv_scratch_close(&scratch);
   CHECK(allocations == frees && !scratch.count && !scratch.heap);
   CHECK(xe_spv_scratch_push(&scratch, 123));
   CHECK(xe_spv_scratch_data(&scratch) == scratch.local);
   xe_spv_scratch_close(&scratch);
   puts("PASS: inline C89 arguments, failed-growth rollback, 10000 retained resize/fill comparisons without allocations");
   return 0;
}
int main(void)
{
   struct {
      xe_spv_word_list list;
      uint32_t canary;
   } storage;
   xe_spv_word_view view;
   uint32_t *stable;
   size_t repeat, i;
   CHECK(!check_retained_scratch());
   storage.canary = 0xC0FFEEu;
   stable = storage.list.words;
   for (repeat = 0; repeat < 100000; ++repeat) {
      storage.list.count = 0;
      view = xe_spv_word_list_view(&storage.list);
      CHECK(view.words == stable && !view.count);
      for (i = 0; i < XE_SPV_HELPER_WORD_CAPACITY; ++i)
         CHECK(xe_spv_word_list_push(&storage.list, (uint32_t)(i + repeat)));
      CHECK(!xe_spv_word_list_push(&storage.list, UINT32_MAX));
      view = xe_spv_word_list_view(&storage.list);
      CHECK(view.words == stable && view.count == XE_SPV_HELPER_WORD_CAPACITY);
      CHECK(storage.canary == 0xC0FFEEu);
      for (i = 0; i < view.count; ++i) CHECK(view.words[i] == (uint32_t)(i + repeat));
      storage.list.words[0] = 17;
      CHECK(view.words[0] == 17);
   }
   puts("PASS: 100000 C89 scratch reuses, borrowed identity, capacity/canary checks");
   return 0;
}
