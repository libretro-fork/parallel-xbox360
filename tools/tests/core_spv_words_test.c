/* Fixed shader-ID scratch must retain its storage and reject overflow. */
#include "../../libretro/core_spv_words.h"
#include <stdio.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main(void)
{
   struct {
      xe_spv_word_list list;
      uint32_t canary;
   } storage;
   xe_spv_word_view view;
   uint32_t *stable;
   size_t repeat, i;
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
