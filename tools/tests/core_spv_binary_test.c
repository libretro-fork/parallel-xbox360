/* Failure injection and exact instruction encoding, including the wire limit. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static size_t allocations;
static int fail_allocation;
static void *test_realloc(void *ptr, size_t bytes)
{
   ++allocations;
   return fail_allocation ? NULL : realloc(ptr, bytes);
}
#define XE_SPV_BINARY_REALLOC test_realloc
#include "../../libretro/core_spv_binary.c"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main(void)
{
   xe_spv_binary out = {0};
   uint32_t operands[65535], *stable;
   char text[17];
   char *long_text;
   uint32_t expected, *copy;
   size_t i, j, before, capacity;
   for (i = 0; i < 65535; ++i) operands[i] = (uint32_t)i ^ 0xDEADBEEFu;
   fail_allocation = 1;
   xe_spv_binary_word(&out, 7);
   CHECK(out.failed && !out.count && !out.words);
   fail_allocation = 0;
   xe_spv_binary_reset(&out);
   CHECK(xe_spv_binary_reserve(&out, 65536));
   stable = out.words;
   capacity = out.capacity;
   before = allocations;
   for (j = 0; j < 256; ++j) {
      for (i = 0; i < 4; ++i) {
         xe_spv_binary_reset(&out);
         xe_spv_binary_instruction(&out, 128, (i & 1) ? 9 : 0,
               (i & 2) ? 10 : 0, operands, 65532);
         CHECK(!out.failed && out.words == stable && out.count == (size_t)(65533 + !!(i & 1) + !!(i & 2)));
         CHECK(out.words[0] == ((uint32_t)out.count << 16 | 128));
         CHECK(!memcmp(out.words + out.count - 65532, operands, 65532 * sizeof(uint32_t)));
         if (i & 1) CHECK(out.words[1] == 9);
         if (i & 2) CHECK(out.words[1 + !!(i & 1)] == 10);
      }
   }
   CHECK(allocations == before);
   xe_spv_binary_instruction(&out, 128, 0, 0, operands, 65535);
   CHECK(out.failed && out.words == stable);
   xe_spv_binary_reset(&out);
   xe_spv_binary_instruction(&out, 65536, 0, 0, NULL, 0);
   CHECK(out.failed && !out.count);
   xe_spv_binary_reset(&out);
   xe_spv_binary_instruction(&out, 1, 0, 0, NULL, 1);
   CHECK(out.failed && !out.count);
   xe_spv_binary_reset(&out);
   xe_spv_binary_instruction(&out, 0, 0, 0, NULL, 0);
   CHECK(!out.failed && out.count == 1 && out.words[0] == 65536);
   fail_allocation = 1;
   CHECK(!xe_spv_binary_reserve(&out, capacity + 1));
   CHECK(out.words == stable && out.count == 1 && out.capacity == capacity);
   xe_spv_binary_word(&out, 17);
   CHECK(out.count == 1);
   fail_allocation = 0;
   xe_spv_binary_reset(&out);
   CHECK(!xe_spv_binary_reserve(&out, (size_t)-1));
   CHECK(out.words == stable);
   xe_spv_binary_reset(&out);
   for (j = 0; j < sizeof(text); ++j) {
      memset(text, 'x', j);
      text[j] = 0;
      xe_spv_binary_reset(&out);
      xe_spv_binary_string(&out, 330, text);
      CHECK(!out.failed && out.count == 2 + j / 4);
      CHECK(out.words[0] == ((uint32_t)out.count << 16 | 330));
      for (i = 0; i <= j / 4; ++i) {
         size_t k;
         expected = 0;
         for (k = 0; k < 4 && i * 4 + k < j; ++k) expected |= (uint32_t)'x' << (k * 8);
         CHECK(out.words[i + 1] == expected);
      }
   }
   copy = (uint32_t *)malloc(out.count * sizeof(uint32_t));
   CHECK(copy);
   memcpy(copy, out.words, out.count * sizeof(uint32_t));
   i = out.count;
   xe_spv_binary_append(&out, copy, i);
   CHECK(!out.failed && out.count == 2 * i && !memcmp(copy, out.words + i, i * sizeof(uint32_t)));
   free(copy);
   xe_spv_binary_append(&out, NULL, 1);
   CHECK(out.failed);
   xe_spv_binary_reset(&out);
   long_text = (char *)malloc(262137);
   CHECK(long_text);
   memset(long_text, 'x', 262136);
   long_text[262135] = 0;
   xe_spv_binary_string(&out, 330, long_text);
   CHECK(!out.failed && out.count == 65535 && out.words[0] == (65535u << 16 | 330));
   long_text[262135] = 'x';
   long_text[262136] = 0;
   xe_spv_binary_string(&out, 330, long_text);
   CHECK(out.failed && out.count == 65535);
   free(long_text);
   xe_spv_binary_reset(&out);
   out.count = (size_t)-1;
   xe_spv_binary_append(&out, operands, 1);
   CHECK(out.failed && out.count == (size_t)-1);
   xe_spv_binary_reset(&out);
   xe_spv_binary_close(&out);
   xe_spv_binary_close(&out);
   CHECK(!out.words && !out.count && !out.capacity && !out.failed);
   puts("PASS: C89 SPIR-V encoding, wire limits, 1024 reused modules, failure rollback");
   return 0;
}
