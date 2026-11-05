/* The page owner retains storage; typed record cleanup runs exactly once. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static size_t allocations, frees, fail_after = (size_t)-1, destroyed;
static void *test_alloc(size_t bytes)
{
   if (allocations == fail_after) return NULL;
   ++allocations;
   return malloc(bytes);
}
static void test_free(void *ptr) { if (ptr) ++frees; free(ptr); }
#define XE_SPV_ARENA_ALLOC test_alloc
#define XE_SPV_ARENA_FREE test_free
#include "../../libretro/core_spv_arena.c"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
static void destroy(void *record)
{
   ++destroyed;
   xe_spv_arena_retire(record);
}
static void forget(void *record) { (void)record; }
static size_t payload_sum;
static void destroy_payload(void *record)
{
   ++destroyed;
   payload_sum += ((unsigned char *)record)[0];
}
int main(int argc, char **argv)
{
   xe_spv_arena arena = {0};
   void *records[2048], *pointer;
   uint32_t offset;
   size_t i, round, before, expected, expected_sum;
   (void)argv;
   CHECK(!xe_spv_arena_alloc(&arena, &offset));
   fail_after = 0;
   CHECK(!xe_spv_arena_reserve(&arena, 1) && !arena.head);
   fail_after = (size_t)-1;
   CHECK(xe_spv_arena_reserve(&arena, 2048));
   CHECK(!xe_spv_arena_reserve(&arena, (size_t)-1));
   before = allocations;
   for (round = 0; round < 64; ++round) {
      CHECK(xe_spv_arena_begin(&arena));
      CHECK(!xe_spv_arena_begin(&arena) && !xe_spv_arena_close(&arena));
      CHECK(!xe_spv_arena_reserve(&arena, 4096));
      for (i = 0; i < 2048; ++i) {
         pointer = xe_spv_arena_alloc(&arena, &offset);
         CHECK(pointer && !((uintptr_t)pointer & 63));
         if (!round) records[i] = pointer;
         CHECK(pointer == records[i]);
         ((unsigned char *)pointer)[0] = (unsigned char)i;
         ((unsigned char *)pointer)[63] = (unsigned char)(i >> 8);
      }
      CHECK(xe_spv_arena_used(&arena) == 2048 && allocations == before);
      for (i = 0; i < 2048; ++i) {
         CHECK(((unsigned char *)records[i])[0] == (unsigned char)i);
         CHECK(((unsigned char *)records[i])[63] == (unsigned char)(i >> 8));
         if (i >= 5) xe_spv_arena_retire(records[i]);
      }
      if (argc > 1 && strcmp(argv[1], "finish-uaf")) return ((volatile unsigned char *)records[100])[0];
      CHECK(!xe_spv_arena_end(&arena, NULL));
      CHECK(!xe_spv_arena_end(&arena, forget));
      CHECK(xe_spv_arena_end(&arena, destroy));
      CHECK(destroyed == (round + 1) * 5);
   }
   CHECK(xe_spv_arena_close(&arena) && xe_spv_arena_close(&arena));
   CHECK(allocations == frees);
   /* Mid-lease growth failure keeps prior records and cleanup intact. */
   CHECK(xe_spv_arena_begin(&arena));
   for (i = 0; i < XE_SPV_RECORDS_PER_PAGE; ++i)
      CHECK(xe_spv_arena_alloc(&arena, &offset));
   fail_after = allocations;
   CHECK(!xe_spv_arena_alloc(&arena, &offset));
   CHECK(xe_spv_arena_used(&arena) == XE_SPV_RECORDS_PER_PAGE);
   CHECK(xe_spv_arena_end(&arena, destroy));
   CHECK(xe_spv_arena_close(&arena) && allocations == frees);
   fail_after = (size_t)-1;
   CHECK(xe_spv_arena_reserve(&arena, 2048));
   before = allocations;
   for (round = 0; round < 16; ++round) {
      CHECK(xe_spv_arena_begin(&arena));
      expected = expected_sum = destroyed = payload_sum = 0;
      for (i = 0; i < 2048; ++i) {
         records[i] = xe_spv_arena_alloc(&arena, &offset);
         CHECK(records[i]);
         ((unsigned char *)records[i])[0] = (unsigned char)i;
         if (i % 3) {
            ++expected;
            expected_sum += (unsigned char)i;
         } else xe_spv_arena_retire(records[i]);
      }
      CHECK(!xe_spv_arena_finish(&arena, NULL));
      CHECK(xe_spv_arena_used(&arena) == 2048);
      CHECK(xe_spv_arena_finish(&arena, destroy_payload));
      CHECK(destroyed == expected && payload_sum == expected_sum);
      CHECK(!arena.in_use && !xe_spv_arena_finish(&arena, destroy_payload));
      CHECK(allocations == before);
      if (argc > 1) return ((volatile unsigned char *)records[1])[0];
   }
   CHECK(xe_spv_arena_begin(&arena) && xe_spv_arena_finish(&arena, NULL));
   CHECK(xe_spv_arena_close(&arena) && allocations == frees);
   puts("PASS: 32768 arena-owned records, sparse payload-only bitmap cleanup and allocation-free reuse");
   puts("PASS: 131072 aligned arena records, reuse, leases, cleanup and allocation faults");
   return 0;
}
