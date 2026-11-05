/* C89: payload leases, stable growth, allocator transitions and failure rollback. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static size_t allocations, frees, fail_after = (size_t)-1;
static void *allocate(size_t n) {
   if (allocations == fail_after) return NULL;
   ++allocations; return malloc(n);
}
static void release(void *p) { if (p) ++frees; free(p); }
#define XE_SPV_ARENA_ALLOC allocate
#define XE_SPV_ARENA_FREE release
#define XE_SPV_ALLOC allocate
#define XE_SPV_FREE release
#include "../../libretro/core_spv_arena.c"
#include "../../libretro/core_spv_operands.c"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); return 1; } } while (0)
static int reserve(xe_spv_arena *arena, xe_spv_operands *ops, size_t n) {
   return xe_spv_operands_reserve_with(ops, n, xe_spv_arena_payload_realloc, arena);
}
int main(int argc, char **argv) {
   xe_spv_arena arena = {0}, other = {0};
   xe_spv_operands a, b;
   uint32_t offset, *stable, *old;
   void *record;
   size_t before, i, round, warm = 0;
   volatile uint32_t value;
   fail_after = 0;
   CHECK(!xe_spv_arena_reserve_payloads(&arena, 1));
   CHECK(xe_spv_arena_close(&arena));
   fail_after = allocations + 1;
   CHECK(!xe_spv_arena_reserve_payloads(&arena, 1));
   CHECK(xe_spv_arena_close(&arena) && allocations == frees);
   fail_after = (size_t)-1;
   CHECK(!xe_spv_arena_reserve_payloads(&arena, SIZE_MAX));
   CHECK(xe_spv_arena_reserve_payloads(&arena, 16384) && xe_spv_arena_reserve(&arena, 1));
   CHECK(xe_spv_arena_reserve_payloads(&other, 16384));
   CHECK(!xe_spv_arena_payload_realloc(&arena, NULL, 0, 16));
   for (round = 0; round < 100; ++round) {
      CHECK(xe_spv_arena_begin(&arena) && xe_spv_arena_begin(&other));
      CHECK(!xe_spv_arena_reserve_payloads(&arena, 32768));
      CHECK(!xe_spv_arena_payload_realloc(&arena, NULL, 0, SIZE_MAX));
      record = xe_spv_arena_alloc(&arena, &offset);
      CHECK(record && xe_spv_arena_record_owner(record, offset) == &arena);
      CHECK(!xe_spv_arena_record_owner(NULL, 0));
      xe_spv_operands_init(&a); xe_spv_operands_init(&b);
      for (i = 0; i < 4; ++i) CHECK(xe_spv_operands_append(&a, (uint32_t)i + 10, (i + round) & 1));
      CHECK(reserve(&arena, &a, 8)); stable = a.heap; before = allocations;
      CHECK(reserve(&arena, &a, 1024) && a.heap == stable && allocations == before);
      for (i = 0; i < 4; ++i) {
         CHECK(a.heap[i] == i + 10);
         CHECK(xe_spv_operands_is_id(&a, (uint32_t)i) == (int)((i + round) & 1));
      }
      CHECK(reserve(&arena, &b, 100));
      CHECK(reserve(&arena, &a, 8192) && a.heap != stable); old = a.heap;
      CHECK(!xe_spv_arena_end(&arena, NULL) && !xe_spv_arena_close(&arena));
      CHECK(old[0] == 10);
      before = allocations;
      if (!round) {
         fail_after = allocations;
         CHECK(!reserve(&arena, &a, 1000000)); fail_after = (size_t)-1;
         CHECK(allocations == before && a.heap == old && a.count == 4);
      }
      CHECK(!reserve(&arena, &a, SIZE_MAX) && a.heap == old);
      if (argc > 1 && !strcmp(argv[1], "grow-uaf")) { value = stable[0]; return (int)value; }
      xe_spv_operands_free(&b);
      if (argc > 1 && !strcmp(argv[1], "free-uaf")) {
         CHECK(reserve(&arena, &b, 10)); stable = b.heap;
         xe_spv_operands_free(&b); value = stable[0]; return (int)value;
      }
      xe_spv_operands_free(&a); xe_spv_arena_retire(record);
      CHECK(xe_spv_arena_end(&arena, NULL));
      if (argc > 1 && !strcmp(argv[1], "end-uaf")) { value = old[0]; return (int)value; }
      CHECK(xe_spv_arena_end(&other, NULL));
      if (!round) warm = allocations;
      CHECK(allocations == warm);
   }
   CHECK(xe_spv_arena_begin(&arena) && xe_spv_arena_begin(&other));
   xe_spv_operands_init(&a);
   CHECK(reserve(&arena, &a, 65));
   for (i = 0; i < 65; ++i) xe_spv_operands_append_reserved(&a, (uint32_t)i, i & 1);
   stable = a.heap;
   CHECK(reserve(&arena, &a, 66) && a.heap == stable);
   for (i = 0; i < 65; ++i) {
      CHECK(a.heap[i] == i && xe_spv_operands_is_id(&a, (uint32_t)i) == (int)(i & 1));
   }
   xe_spv_operands_free(&a);
   CHECK(reserve(&arena, &a, 10) && a.local_flags[0]);
   CHECK(xe_spv_operands_append(&a, 17, 1));
   CHECK(xe_spv_operands_reserve(&a, 100) && !a.local_flags[0]);
   CHECK(a.heap[0] == 17 && xe_spv_operands_is_id(&a, 0));
   CHECK(reserve(&other, &a, 1000) && a.local_flags[0]);
   CHECK(a.heap[0] == 17 && xe_spv_operands_is_id(&a, 0));
   CHECK(reserve(&arena, &a, 2000) && a.local_flags[0]);
   CHECK(a.heap[0] == 17 && xe_spv_operands_is_id(&a, 0));
   xe_spv_operands_free(&a);
   CHECK(xe_spv_arena_end(&arena, NULL) && xe_spv_arena_end(&other, NULL));
   CHECK(xe_spv_arena_close(&arena) && xe_spv_arena_close(&other));
   CHECK(allocations == frees);
   puts("PASS: retained payloads, stable/copy growth, packed flags, faults and allocator transitions");
   return 0;
}
