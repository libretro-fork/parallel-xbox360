#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "../../libretro/core_spv_geometry.h"
#include <stdio.h>
#include <limits.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d\n", __LINE__); return 1; } } while (0)
int main(void) {
   unsigned type, interpolators, planes, flags, output, index;
   size_t count, expected;
   char guarded[34], text[32];
   int valid;
   for (type = 0; type < 6; ++type)
      for (interpolators = 0; interpolators < 18; ++interpolators)
         for (planes = 0; planes < 8; ++planes)
            for (flags = 0; flags < 8; ++flags) {
               valid = type >= 1 && type <= 4 && interpolators <= 16 && planes <= 6;
               count = (size_t)-1;
               CHECK(xe_spv_geometry_plan(type, interpolators, planes,
                     flags & 1, flags & 2, flags & 4, &count) == valid);
               expected = valid ? interpolators * 2 + 2 + !!(flags & 1) +
                     !!(flags & 2) + ((type == 1 || type == 4) && (flags & 4)) : 0;
               CHECK(count == expected && count <= 37);
            }
   CHECK(!xe_spv_geometry_plan(1, 0, 0, 0, 0, 0, NULL));
   CHECK(!xe_spv_geometry_plan(1, UINT_MAX, 0, 0, 0, 0, &count) && !count);
   CHECK(!xe_spv_geometry_plan(1, 0, UINT_MAX, 0, 0, 0, &count) && !count);
   for (output = 0; output < 2; ++output)
      for (index = 0; index < 16; ++index) {
         memset(guarded, 'X', sizeof(guarded));
         sprintf(text, "xe_%s_interpolator_%u", output ? "out" : "in", index);
         CHECK(xe_spv_geometry_name(guarded + 1, 32, output, index));
         CHECK(!strcmp(guarded + 1, text) && guarded[0] == 'X' && guarded[33] == 'X');
         memset(guarded, 'X', sizeof(guarded));
         CHECK(!xe_spv_geometry_name(guarded + 1, strlen(text), output, index));
         for (flags = 0; flags < sizeof(guarded); ++flags) CHECK(guarded[flags] == 'X');
      }
   CHECK(!xe_spv_geometry_name(NULL, 32, 0, 0));
   CHECK(!xe_spv_geometry_name(guarded, sizeof(guarded), 0, 16));
   puts("PASS: C89 geometry bounds, exact interface counts and allocation-free checked names");
   return 0;
}
