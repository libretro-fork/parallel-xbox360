#define _CRT_SECURE_NO_WARNINGS
/* C89 independent ordering/wire oracle and module ownership/failure checks. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static size_t calls, live, fail_at = (size_t)-1;
static void *allocate(size_t bytes) {
   void *p;
   if (calls++ == fail_at) return NULL;
   p = malloc(bytes); if (p) ++live; return p;
}
static void release(void *p) { if (p) { --live; free(p); } }
#define XE_SPV_ARENA_ALLOC allocate
#define XE_SPV_ARENA_FREE release
#include "../../libretro/core_spv_arena.c"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"decoration line %u: %s\n",(unsigned)__LINE__,#x); return 1; } } while(0)
typedef struct expected_record { uint32_t words[5], flags; } expected_record;
static int reference_order(const void *a, const void *b) {
   const expected_record *x = (const expected_record *)a, *y = (const expected_record *)b;
   size_t i;
   if (x->words[1] != y->words[1]) return x->words[1] < y->words[1] ? -1 : 1;
   if ((x->words[0] & 65535) != (y->words[0] & 65535)) return (x->words[0] & 65535) < (y->words[0] & 65535) ? -1 : 1;
   for (i = 2; i < 5; ++i) {
      unsigned xid = (x->flags >> i) & 1, yid = (y->flags >> i) & 1;
      if (xid != yid) return xid < yid ? -1 : 1;
      if (x->words[i] != y->words[i]) return x->words[i] < y->words[i] ? -1 : 1;
   }
   return 0;
}
static int add_value(xe_spv_arena *arena, uint32_t target, uint32_t op, uint32_t decoration,
      uint32_t value, uint32_t member, int structure) {
   xe_spv_decoration_key key;
   xe_spv_decoration_key_init(&key,op,target,decoration);
   if (structure) { key.prefix[1] = decoration; key.prefix[0] = member; key.prefix_count = 2; }
   key.values = xe_spv_words_view(&value,1);
   return xe_spv_arena_add_decoration(arena,&key);
}
static int failures(void) {
   xe_spv_arena arena = {0};
   xe_spv_decoration_key key;
   xe_spv_record_view view;
   const xe_spv_decoration *saved;
   uint32_t *values;
   size_t step, before, i;
   for (step = 0; step < 7; ++step) {
      CHECK(xe_spv_arena_begin(&arena)); fail_at = calls + step;
      if (!add_value(&arena,1,XE_SPV_DECORATE,3,4,0,0)) CHECK(!xe_spv_arena_decorations(&arena).count);
      fail_at = (size_t)-1;
      CHECK(xe_spv_arena_end(&arena,NULL) && xe_spv_arena_close(&arena) && !live);
   }
   CHECK(xe_spv_arena_reserve_payloads(&arena,16384));
   CHECK(xe_spv_arena_reserve_decorations(&arena,4)); CHECK(xe_spv_arena_begin(&arena));
   for (i = 0; i < 4; ++i) CHECK(add_value(&arena,(uint32_t)i,71,3,4,0,0));
   saved = (const xe_spv_decoration *)xe_spv_arena_decorations(&arena).records[0];
   fail_at = calls; CHECK(!add_value(&arena,4,71,3,4,0,0)); fail_at = (size_t)-1;
   CHECK(xe_spv_arena_decorations(&arena).count == 4);
   CHECK(add_value(&arena,4,71,3,4,0,0));
   values = (uint32_t *)malloc(65533 * sizeof(*values)); CHECK(values);
   for (i = 0; i < 65533; ++i) values[i] = (uint32_t)i;
   xe_spv_decoration_key_init(&key,71,77,3); key.values = xe_spv_words_view(values,65532);
   fail_at = calls; CHECK(!xe_spv_arena_add_decoration(&arena,&key)); fail_at = (size_t)-1;
   view = xe_spv_arena_decorations(&arena); CHECK(view.count == 5 && view.records[0] == saved && xe_spv_decoration_words(saved)[1] == 0);
   CHECK(xe_spv_arena_add_decoration(&arena,&key));
   CHECK(xe_spv_decoration_view((const xe_spv_decoration *)xe_spv_arena_decorations(&arena).records[5]).count == 65535);
   key.values.count = 65533; before = calls;
   CHECK(!xe_spv_arena_add_decoration(&arena,&key) && calls == before);
   key.values.count = 0; key.strings = NULL; key.string_count = 65535;
   CHECK(!xe_spv_arena_add_decoration(&arena,&key) && calls == before);
   key.string_count = 0; key.values.count = 1; key.values.words = NULL;
   CHECK(!xe_spv_arena_add_decoration(&arena,&key) && calls == before);
   free(values); CHECK(xe_spv_arena_end(&arena,NULL) && xe_spv_arena_close(&arena) && !live);
   return 0;
}
int main(int argc, char **argv) {
   xe_spv_arena arena = {0};
   xe_spv_record_view view;
   xe_spv_decoration_key key;
   expected_record expected[1024];
   uint32_t values[2], word;
   const xe_spv_decoration *saved;
   const uint32_t *words;
   size_t round, i, j, before, warmed = 0, count, slot, bytes, start;
   int found;
   char input[260]; const char *text = input + 1;
   volatile uint32_t stale;
   CHECK(!failures());
   CHECK(xe_spv_arena_reserve_payloads(&arena,16384));
   CHECK(xe_spv_arena_reserve_decorations(&arena,128));
   for (round = 0; round < 10; ++round) {
      CHECK(xe_spv_arena_begin(&arena)); CHECK(!xe_spv_arena_decorations(&arena).count);
      for (i = 0; i < 1024; ++i) {
         xe_spv_decoration_key_init(&key,i & 1 ? 332 : 71,(uint32_t)(i % 17), (uint32_t)(i % 13));
         values[0] = (uint32_t)i; values[1] = (uint32_t)(i ^ UINT32_C(0x80000000));
         key.values = xe_spv_words_view(values,2); key.values_are_ids = (i % 3) == 0;
         CHECK(xe_spv_arena_add_decoration(&arena,&key));
         expected[i].words[0] = (5u << 16) | key.opcode; expected[i].words[1] = key.target;
         expected[i].words[2] = key.prefix[0]; expected[i].words[3] = values[0]; expected[i].words[4] = values[1];
         expected[i].flags = key.values_are_ids ? 24 : 0;
      }
      qsort(expected,1024,sizeof(*expected),reference_order);
      view = xe_spv_arena_decorations(&arena); CHECK(view.count == 1024 && !xe_spv_arena_used(&arena));
      for (i = 0; i < 1024; ++i) CHECK(!memcmp(xe_spv_decoration_words((const xe_spv_decoration *)view.records[i]),expected[i].words,20));
      saved = (const xe_spv_decoration *)view.records[0]; before = calls;
      for (j = 0; j < 10; ++j) for (i = 0; i < 1024; ++i) {
         xe_spv_decoration_key_init(&key,i & 1 ? 332 : 71,(uint32_t)(i % 17),(uint32_t)(i % 13));
         values[0] = (uint32_t)i; values[1] = (uint32_t)(i ^ UINT32_C(0x80000000));
         key.values = xe_spv_words_view(values,2); key.values_are_ids = i % 3 == 0;
         CHECK(xe_spv_arena_add_decoration(&arena,&key));
      }
      CHECK(calls == before && xe_spv_arena_decorations(&arena).count == 1024);
      /* Counted, unaligned, embedded-zero text preserves literal word identity.
       * ID flags distinguish otherwise equal wire encodings. */
      for (bytes = 0; bytes < 130; ++bytes) {
         for (i = 0; i < bytes; ++i) input[i + 1] = (char)(i * 31);
         xe_spv_decoration_key_init(&key,71,900,41); key.strings = &text; key.string_count = 1;
         key.first_string_counted = 1; key.first_string_bytes = bytes; key.has_suffix = 1; key.suffix = 2;
         CHECK(xe_spv_arena_add_decoration(&arena,&key) && xe_spv_decoration_measure(&key,&count));
         view = xe_spv_arena_decorations(&arena); slot = xe_spv_decorations_locate(view,&key,count,&found); CHECK(found);
         words = xe_spv_decoration_words((const xe_spv_decoration *)view.records[slot]);
         CHECK(words[0] >> 16 == count && words[count - 1] == 2);
         for (i = 0; i < bytes / 4 + 1; ++i) {
            word = 0; start = i * 4;
            for (j = 0; j < 4 && start + j < bytes; ++j) word |= (uint32_t)(unsigned char)input[start + j + 1] << (j * 8);
            CHECK(words[3 + i] == word);
         }
         memset(input,'X',sizeof(input));
      }
      CHECK(xe_spv_arena_end(&arena,NULL)); CHECK(!xe_spv_arena_decorations(&arena).count);
      if (argc > 1 && !strcmp(argv[1],"uaf")) { stale = saved->id_begin; return (int)stale; }
      if (!round) warmed = calls;
      CHECK(calls == warmed);
   }
   CHECK(xe_spv_arena_close(&arena) && !live);
   puts("PASS: C89 decoration wire/order oracle, borrowed keys, reuse, faults, limits and lifetime");
   return 0;
}
