#include <stdio.h>
#include "../../libretro/core_spv_size.c"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main(void)
{
   xe_spv_count size = {0, 0};
   size_t repeat, type, result;
   for (repeat = 0; repeat < 10000; ++repeat) {
      for (type = 0; type < 2; ++type) {
         for (result = 0; result < 2; ++result) {
            size.count = 0;
            xe_spv_count_instruction(&size, 80, (uint32_t)type, (uint32_t)result, 129);
            CHECK(!size.failed && size.count == 130 + type + result);
         }
      }
   }
   size.count = 0;
   xe_spv_count_instruction(&size, 80, 1, 2, 65532);
   CHECK(!size.failed && size.count == 65535);
   xe_spv_count_instruction(&size, 80, 1, 2, 65533);
   CHECK(size.failed && size.count == 65535);
   size.failed = 0;
   xe_spv_count_instruction(&size, 65536, 0, 0, 0);
   CHECK(size.failed && size.count == 65535);
   size.count = (size_t)-1 / sizeof(uint32_t); size.failed = 0;
   xe_spv_count_words(&size, 1);
   CHECK(size.failed);
   size.count = 0; size.failed = 0;
   xe_spv_count_source(&size, 0, 300000);
   CHECK(!size.failed && size.count == 3);
   size.count = 0;
   xe_spv_count_source(&size, 1, 0);
   CHECK(!size.failed && size.count == 4);
   size.count = 0;
   xe_spv_count_source(&size, 1, 300000);
   CHECK(!size.failed && size.count == 75006);
   size.count = 0;
   xe_spv_count_source(&size, 1, XE_SPV_SOURCE_CHUNK_BYTES);
   CHECK(!size.failed && size.count == 65535);
   size.count = 0;
   xe_spv_count_source(&size, 1, 2u * XE_SPV_SOURCE_CHUNK_BYTES);
   CHECK(!size.failed && size.count == 131067);
   size.count = 75006;
   xe_spv_count_source(&size, 1, (size_t)-1);
   CHECK(size.failed && size.count == 75006);
   puts("PASS: 40000 C89 instruction sizes, source chunks, wire limits, failure latching and overflow");
   return 0;
}
