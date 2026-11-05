#include "core_spv_operands.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %u\n", (unsigned)__LINE__); return 1; } } while (0)
static unsigned allocations, live, fail_next;
static size_t last_size;
void *xe_spv_test_alloc(size_t size) {
   size_t *p;
   ++allocations; last_size = size;
   if (fail_next) { fail_next = 0; return NULL; }
   p = (size_t *)malloc(sizeof(*p) + size);
   if (!p) return NULL;
   *p = size; ++live;
   memset(p + 1, 0xAD, size);
   return p + 1;
}
void xe_spv_test_free(void *value) {
   if (!value) return;
   --live;
   free((size_t *)value - 1);
}
static int strings(void) {
   xe_spv_operands operands;
   unsigned char source[1057];
   size_t prefix, bytes, i, j, word, before;
   uint32_t expected;
   const uint32_t *old;
   for (i = 0; i < sizeof(source); ++i) source[i] = (unsigned char)(i * 37 + 1);
   for (prefix = 0; prefix < 18; ++prefix) {
      for (bytes = 0; bytes <= sizeof(source); ++bytes) {
         xe_spv_operands_init(&operands);
         for (i = 0; i < prefix; ++i) CHECK(xe_spv_operands_append(&operands, (uint32_t)i + 1, i & 1));
         before = allocations;
         CHECK(xe_spv_operands_string(&operands, (const char *)source, bytes));
         CHECK(allocations - before <= 1 && operands.count == prefix + bytes / 4 + 1);
         for (i = 0; i < prefix; ++i) {
            CHECK(xe_spv_operands_const_words(&operands)[i] == i + 1);
            CHECK(xe_spv_operands_is_id(&operands, (uint32_t)i) == (int)(i & 1));
         }
         for (word = 0; word <= bytes / 4; ++word) {
            expected = 0;
            for (j = 0; j < 4; ++j) {
               i = word * 4 + j;
               if (i < bytes) expected |= (uint32_t)source[i] << (j * 8);
            }
            CHECK(xe_spv_operands_const_words(&operands)[prefix + word] == expected);
            CHECK(!xe_spv_operands_is_id(&operands, (uint32_t)(prefix + word)));
         }
         CHECK(xe_spv_operands_append(&operands, 123, 1));
         CHECK(xe_spv_operands_is_id(&operands, operands.count - 1));
         xe_spv_operands_free(&operands);
         CHECK(!live);
      }
   }
   xe_spv_operands_init(&operands);
   CHECK(xe_spv_operands_append(&operands, 17, 1));
   old = xe_spv_operands_const_words(&operands);
   fail_next = 1;
   CHECK(!xe_spv_operands_string(&operands, (const char *)source, sizeof(source)));
   CHECK(operands.count == 1 && operands.capacity == 4 && old == xe_spv_operands_const_words(&operands));
   CHECK(old[0] == 17 && xe_spv_operands_is_id(&operands, 0));
   before = allocations;
   CHECK(!xe_spv_operands_string(&operands, NULL, 1));
   CHECK(!xe_spv_operands_string(&operands, (const char *)source, SIZE_MAX));
   CHECK(before == allocations && operands.count == 1);
   CHECK(xe_spv_operands_string(&operands, NULL, 0) && operands.count == 2);
   CHECK(xe_spv_operands_is_id(&operands, 0) && !xe_spv_operands_is_id(&operands, 1));
   xe_spv_operands_free(&operands);
   return 0;
}
int main(void) {
   xe_spv_operands operands;
   unsigned i, before;
   const uint32_t *old;
   xe_spv_operands_init(&operands);
   CHECK(xe_spv_operands_reserve(&operands, 0));
   for (i = 0; i < 4; ++i) CHECK(xe_spv_operands_append(&operands, i * 37, i & 1));
   CHECK(!allocations && !live && !operands.heap);
   fail_next = 1;
   CHECK(!xe_spv_operands_append(&operands, 999, 1) && operands.count == 4 && !operands.heap);
   for (i = 0; i < 4; ++i) CHECK(xe_spv_operands_const_words(&operands)[i] == i * 37);
   CHECK(xe_spv_operands_append(&operands, 148, 0) && live == 1 && last_size == 33);
   before = allocations;
   CHECK(xe_spv_operands_reserve(&operands, 8) && allocations == before);
   old = xe_spv_operands_const_words(&operands);
   fail_next = 1;
   CHECK(!xe_spv_operands_reserve(&operands, 1025) && xe_spv_operands_const_words(&operands) == old);
   CHECK(xe_spv_operands_reserve(&operands, 1025) && live == 1 && last_size == 4229);
   for (i = 5; i < 10000; ++i) CHECK(xe_spv_operands_append(&operands, i * 37, i & 1));
   for (i = 0; i < 10000; ++i) {
      CHECK(xe_spv_operands_const_words(&operands)[i] == i * 37);
      CHECK(xe_spv_operands_is_id(&operands, i) == (int)(i & 1));
   }
   before = allocations; old = xe_spv_operands_const_words(&operands);
   CHECK(!xe_spv_operands_reserve(&operands, SIZE_MAX) && allocations == before);
   CHECK(xe_spv_operands_const_words(&operands) == old && operands.count == 10000);
   xe_spv_operands_free(&operands); xe_spv_operands_free(&operands);
   CHECK(!live && !operands.heap && !operands.count && operands.capacity == 4);
   CHECK(xe_spv_operands_append(&operands, UINT32_MAX, -7));
   CHECK(xe_spv_operands_const_words(&operands)[0] == UINT32_MAX && xe_spv_operands_is_id(&operands, 0) == 1);
   xe_spv_operands_free(&operands);
   CHECK(!strings());
   puts("PASS: inline/growing operands, flags, poisoned storage, allocation failure, bounds and reuse");
   return 0;
}
