#define _CRT_SECURE_NO_WARNINGS
/* C89: encoded ownership, sorting, reuse, lease lifetime and rollback. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static size_t calls, live, fail_at = (size_t)-1;
static void *allocate(size_t bytes) {
   void *value;
   if (calls++ == fail_at) return NULL;
   value = malloc(bytes);
   if (value) ++live;
   return value;
}
static void release(void *value) { if (value) { --live; free(value); } }
#define XE_SPV_ARENA_ALLOC allocate
#define XE_SPV_ARENA_FREE release
#include "../../libretro/core_spv_arena.c"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"extension line %u: %s\n",(unsigned)__LINE__,#x); return 1; } } while(0)
static int failures(void) {
   xe_spv_arena arena;
   xe_spv_record_view view;
   xe_spv_extension *first;
   size_t step, before, i;
   char name[64], *large;
   for (step = 0; step < 7; ++step) {
      memset(&arena,0,sizeof(arena));
      CHECK(xe_spv_arena_begin(&arena));
      fail_at = calls + step;
      if (!xe_spv_arena_add_extension(&arena,"SPV_FAKE_cold_allocation_failure"))
         CHECK(!xe_spv_arena_extensions(&arena).count);
      fail_at = (size_t)-1;
      CHECK(xe_spv_arena_end(&arena,NULL) && xe_spv_arena_close(&arena));
      CHECK(!live);
   }
   memset(&arena,0,sizeof(arena));
   CHECK(xe_spv_arena_reserve_payloads(&arena,16384));
   CHECK(xe_spv_arena_reserve_extensions(&arena,16));
   CHECK(xe_spv_arena_begin(&arena));
   for (i = 0; i < 16; ++i) {
      sprintf(name,"SPV_FAKE_name_%03u",(unsigned)i);
      CHECK(xe_spv_arena_add_extension(&arena,name));
   }
   first = xe_spv_extensions_find(xe_spv_arena_extensions(&arena),"SPV_FAKE_name_000");
   CHECK(first);
   before = calls; fail_at = calls;
   CHECK(!xe_spv_arena_add_extension(&arena,"SPV_FAKE_name_016"));
   CHECK(calls == before + 1 && xe_spv_arena_extensions(&arena).count == 16);
   CHECK(first == xe_spv_extensions_find(xe_spv_arena_extensions(&arena),"SPV_FAKE_name_000"));
   fail_at = (size_t)-1;
   CHECK(xe_spv_arena_add_extension(&arena,"SPV_FAKE_name_016"));
   large = (char *)malloc(262137);
   CHECK(large);
   memset(large,'Q',262136); large[200000] = 0;
   fail_at = calls;
   CHECK(!xe_spv_arena_add_extension(&arena,large));
   view = xe_spv_arena_extensions(&arena);
   CHECK(view.count == 17 && !xe_spv_extensions_find(view,large));
   CHECK(first == xe_spv_extensions_find(view,"SPV_FAKE_name_000") && first->active);
   fail_at = (size_t)-1;
   CHECK(xe_spv_arena_add_extension(&arena,large));
   large[200000] = 'Q'; large[262135] = 0;
   CHECK(xe_spv_arena_add_extension(&arena,large));
   CHECK(xe_spv_extension_view(xe_spv_extensions_find(xe_spv_arena_extensions(&arena),large)).count == 65535);
   large[262135] = 'Q'; large[262136] = 0;
   before = calls;
   CHECK(!xe_spv_arena_add_extension(&arena,large) && calls == before);
   CHECK(xe_spv_arena_extensions(&arena).count == 19);
   free(large);
   CHECK(xe_spv_arena_end(&arena,NULL) && xe_spv_arena_close(&arena));
   CHECK(!live);
   return 0;
}
int main(int argc, char **argv) {
   xe_spv_arena arena = {0};
   xe_spv_record_view view;
   xe_spv_extension *saved;
   xe_spv_word_view wire;
   char name[64];
   size_t round, i, before, warmed = 0;
   volatile uint32_t stale;
   CHECK(!failures());
   CHECK(!xe_spv_arena_add_extension(&arena,"outside_lease"));
   CHECK(xe_spv_arena_reserve_payloads(&arena,16384));
   CHECK(xe_spv_arena_reserve_extensions(&arena,XE_SPV_EXTENSION_COLD_CAPACITY));
   for (round = 0; round < 100; ++round) {
      CHECK(xe_spv_arena_begin(&arena));
      CHECK(!xe_spv_arena_extensions(&arena).count && !xe_spv_arena_used(&arena));
      for (i = 129; i; --i) {
         sprintf(name,"SPV_FAKE_name_%03u",(unsigned)(i - 1));
         CHECK(xe_spv_arena_add_extension(&arena,name));
         memset(name,'X',sizeof(name));
      }
      CHECK(xe_spv_arena_add_extension(&arena,""));
      CHECK(xe_spv_arena_add_extension(&arena,"SPV_UTF8_\x80\xff"));
      view = xe_spv_arena_extensions(&arena);
      CHECK(view.count == 131 && !xe_spv_arena_used(&arena));
      saved = xe_spv_extensions_find(view,"SPV_FAKE_name_000"); CHECK(saved);
      wire = xe_spv_extension_view(saved);
      CHECK((wire.words[0] & 65535) == XE_SPV_EXTENSION_OPCODE && wire.count == 6);
      CHECK(!xe_spv_extension_compare((const xe_spv_extension *)view.records[0],""));
      for (i = 0; i < 129; ++i) {
         sprintf(name,"SPV_FAKE_name_%03u",(unsigned)i);
         CHECK(!xe_spv_extension_compare((const xe_spv_extension *)view.records[i + 1],name));
      }
      before = calls;
      for (i = 0; i < 1000; ++i) {
         xe_spv_arena_remove_extension(&arena,"SPV_FAKE_name_000");
         CHECK(!saved->active);
         xe_spv_arena_remove_extension(&arena,"not_present");
         CHECK(xe_spv_arena_add_extension(&arena,"SPV_FAKE_name_000"));
         CHECK(saved->active && xe_spv_extension_view(saved).words == wire.words);
      }
      CHECK(calls == before && xe_spv_arena_extensions(&arena).count == 131);
      CHECK(!xe_spv_arena_add_extension(&arena,NULL));
      xe_spv_arena_remove_extension(&arena,NULL);
      CHECK(xe_spv_arena_end(&arena,NULL));
      CHECK(!xe_spv_arena_extensions(&arena).count);
      if (argc > 1 && !strcmp(argv[1],"uaf")) { stale = saved->active; return (int)stale; }
      if (!round) warmed = calls;
      CHECK(calls == warmed);
   }
   CHECK(xe_spv_arena_close(&arena) && !live);
   puts("PASS: encoded extensions, sorted ownership, disable/re-add reuse, warm leases, faults and wire limits");
   return 0;
}
