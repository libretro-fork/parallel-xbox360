/* Generic C89 object storage: typed owners are simulated with independent
 * payloads and function->block destruction, including detached objects. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static size_t allocations, frees, fail_after = (size_t)-1;
static void *allocate(size_t bytes) {
   if (allocations == fail_after) return NULL;
   ++allocations;
   return malloc(bytes);
}
static void release(void *p) { if (p) ++frees; free(p); }
#define XE_SPV_ARENA_ALLOC allocate
#define XE_SPV_ARENA_FREE release
#include "../../libretro/core_spv_arena.c"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
typedef struct payload { unsigned id, dead; struct payload *child; } payload;
static unsigned functions, blocks, bad;
static size_t sum;
static void destroy_block(void *p) {
   payload *value = (payload *)p;
   if (value->dead) bad = 1;
   value->dead = 1;
   ++blocks;
   sum += value->id;
   xe_spv_arena_object_retire(p);
}
static void destroy_function(void *p) {
   payload *value = (payload *)p;
   if (value->dead) bad = 1;
   value->dead = 1;
   ++functions;
   sum += value->id;
   if (value->child) destroy_block(value->child);
   xe_spv_arena_object_retire(p);
}
int main(int argc, char **argv) {
   xe_spv_arena arena = {0};
   payload *first_blocks[384], *first_functions[256], *value;
   unsigned round, i;
   size_t cold;
   (void)argv;
   CHECK(!xe_spv_arena_object_alloc(&arena, XE_SPV_OBJECT_BLOCK, 136));
   fail_after = 0;
   CHECK(!xe_spv_arena_reserve_objects(&arena, 136, 64, 208, 32));
   CHECK(xe_spv_arena_close(&arena));
   fail_after = allocations + 1;
   CHECK(!xe_spv_arena_reserve_objects(&arena, 136, 64, 208, 32));
   CHECK(xe_spv_arena_close(&arena) && allocations == frees);
   fail_after = (size_t)-1;
   CHECK(!xe_spv_arena_reserve_objects(&arena, 136, (size_t)-1, 208, 1));
   CHECK(xe_spv_arena_close(&arena) && allocations == frees);
   CHECK(xe_spv_arena_reserve_objects(&arena, 136, 384, 208, 256));
   CHECK(!xe_spv_arena_reserve_objects(&arena, 128, 384, 208, 256));
   cold = allocations;
   for (round = 0; round < 100; ++round) {
      CHECK(xe_spv_arena_begin(&arena));
      CHECK(!xe_spv_arena_reserve_objects(&arena, 136, 384, 208, 256));
      CHECK(!xe_spv_arena_object_alloc(&arena, 2, 136));
      CHECK(!xe_spv_arena_object_alloc(&arena, XE_SPV_OBJECT_BLOCK, 128));
      functions = blocks = bad = 0;
      sum = 0;
      for (i = 0; i < 384; ++i) {
         value = (payload *)xe_spv_arena_object_alloc(&arena, XE_SPV_OBJECT_BLOCK, 136);
         CHECK(value && !((uintptr_t)value & 15));
         if (!round) first_blocks[i] = value;
         CHECK(first_blocks[i] == value);
         value->id = i; value->dead = 0; value->child = NULL;
      }
      for (i = 0; i < 256; ++i) {
         value = (payload *)xe_spv_arena_object_alloc(&arena, XE_SPV_OBJECT_FUNCTION, 208);
         CHECK(value && !((uintptr_t)value & 15));
         if (!round) first_functions[i] = value;
         CHECK(first_functions[i] == value);
         value->id = i; value->dead = 0; value->child = first_blocks[i];
      }
      CHECK(!xe_spv_arena_finish(&arena, NULL) && !xe_spv_arena_close(&arena));
      CHECK(!xe_spv_arena_objects_end(&arena, NULL, destroy_block));
      for (i = 0; i < 256; i += 4) destroy_function(first_functions[i]);
      CHECK(xe_spv_arena_objects_end(&arena, destroy_function, destroy_block));
      CHECK(!bad && functions == 256 && blocks == 384);
      CHECK(sum == 255 * 128 + 383 * 192);
      CHECK(allocations == cold);
      if (argc > 1) return ((volatile unsigned char *)first_blocks[0])[0];
      CHECK(xe_spv_arena_finish(&arena, NULL));
   }
   CHECK(xe_spv_arena_close(&arena) && allocations == frees);
   puts("PASS: 64000 stable pooled objects, parent/child cleanup, orphans, faults and allocation-free warm leases");
   return 0;
}
