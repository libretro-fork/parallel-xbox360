#include <stdio.h>
#include "../../libretro/core_spv_id_range.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main(void)
{
   xe_spv_id_range range = {0, 0};
   size_t count, p, oversized = UINT32_MAX;
   CHECK(sizeof(range) == 8 && !xe_spv_id_range_get(&range, 0));
   for (count = 0; count < 2048; ++count) {
      CHECK(xe_spv_id_range_set(&range, 17, count));
      CHECK(range.count == count && range.first == (count ? 17u : 0u));
      for (p = 0; p < count; ++p) CHECK(xe_spv_id_range_get(&range, p) == 17 + p);
      CHECK(!xe_spv_id_range_get(&range, count));
      CHECK(!xe_spv_id_range_get(&range, (size_t)-1));
   }
   CHECK(xe_spv_id_range_set(&range, UINT32_MAX - 3, 4));
   CHECK(xe_spv_id_range_get(&range, 3) == UINT32_MAX);
   CHECK(!xe_spv_id_range_set(&range, UINT32_MAX - 3, 5));
   CHECK(!xe_spv_id_range_set(&range, 0, 1));
   CHECK(range.first == UINT32_MAX - 3 && range.count == 4);
   if (oversized < (size_t)-1) {
      ++oversized;
      CHECK(!xe_spv_id_range_set(&range, 1, oversized));
   }
   CHECK(xe_spv_id_range_set(&range, 1, UINT32_MAX));
   CHECK(xe_spv_id_range_get(&range, UINT32_MAX - 1) == UINT32_MAX);
   CHECK(xe_spv_id_range_set(&range, 0, 0) && !range.first && !range.count);
   puts("PASS: C89 parameter ID ranges, 2048 lengths, bounds, zero IDs and overflow rollback");
   return 0;
}
