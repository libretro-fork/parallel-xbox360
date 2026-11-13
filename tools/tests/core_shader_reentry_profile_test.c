/* Real Shader::AnalyzeUcode C++ metadata allocations, outside timing. C89.
 * This captures operator-new allocations, not all core malloc/driver calls. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "shader_call_fixture.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"reentry profile line %u\n",(unsigned)__LINE__); exit(1); } } while (0)
int xe_reentry_profile(const uint32_t*,size_t,unsigned,uint64_t*,size_t,size_t*);
static uint32_t code[1600];
static uint64_t masks[1024];
static size_t stats[7];
static void record(FILE *file,const char *kind,unsigned pixel,unsigned mode,
      unsigned repeat)
{
   size_t i;
   uint64_t identity = UINT64_C(14695981039346656037);
   for (i = 0; i < stats[2]; ++i)
      identity = (identity ^ masks[i]) * UINT64_C(1099511628211);
   identity = (identity ^ stats[5]) * UINT64_C(1099511628211);
   identity = (identity ^ stats[6]) * UINT64_C(1099511628211);
   fprintf(file,"%s,%u,%u,%u,%lu,%lu,%lu,%lu,%016llx\n",kind,pixel,mode,
         repeat,(unsigned long)stats[0],(unsigned long)stats[1],
         (unsigned long)stats[2],(unsigned long)stats[3],
         (unsigned long long)identity);
}
int main(int argc,char **argv)
{
   unsigned pixel, mode, n, i, repeated, forward;
   size_t words;
   FILE *file;
   char path[2048];
   static const unsigned counts[] = {16, 64, 256, 512};
   CHECK(argc == 2 && strlen(argv[1]) + 32 < sizeof(path));
   strcpy(path, argv[1]); strcat(path, "/allocations.csv");
   file = fopen(path, "wb"); CHECK(file != NULL);
   /* Keep existing nested calls in the real analyzer matrix. */
   for (pixel = 0; pixel < 2; ++pixel) {
      for (mode = 0; mode < 5; ++mode) {
         words = nested_scenario(code, pixel, mode);
         CHECK(xe_reentry_profile(code, words, pixel, masks, 1024, stats));
         record(file, "nested", pixel, mode, 0);
      }
      for (mode = 0; mode < 4; ++mode) {
         n = counts[mode]; words = (n / 2 + 1) * 3;
         memset(code, 0, sizeof(code));
         /* Overlapping backward conditional jumps with one ALU write per
          * four CFs. Analysis must preserve that write when labels reenter. */
         for (i = 0; i + 1 < n; ++i) {
            if (!(i % 4)) cf(code, i, (n / 2) | (1u << 12), 0x1000);
            else cf(code, i, i > 2 ? i - 2 : 0, 0xb000);
         }
         cf(code, n - 1, (n / 2) | (1u << 12), 0x2000);
         code[n / 2 * 3] = UINT32_C(0xc80f0000);
         code[n / 2 * 3 + 2] = UINT32_C(0xc0000100);
         for (repeated = 0; repeated < 2; ++repeated) {
            CHECK(xe_reentry_profile(code, words, pixel, masks, 1024, stats));
            CHECK(stats[2] == n && stats[3] != 0);
            for (i = 0; i + 1 < n; ++i)
               if (masks[i]) CHECK(masks[i] == UINT64_C(15));
            record(file, "backward", pixel, n, repeated);
         }
      }
      /* Straight/forward-only graphs still collect register and ALU metadata,
       * but no label can be reentered. Exercise inline and high-label storage. */
      for (forward = 0; forward < 2; ++forward) for (mode = 0; mode < 4; ++mode) {
         n = counts[mode]; words = (n / 2 + 1) * 3;
         memset(code, 0, sizeof(code));
         for (i = 0; i + 1 < n; ++i) {
            if (!(i % 4)) cf(code, i, (n / 2) | (1u << 12), 0x1000);
            else if (forward) cf(code, i, i + 2 < n ? i + 2 : n - 1, 0xb000);
         }
         cf(code, n - 1, (n / 2) | (1u << 12), 0x2000);
         code[n / 2 * 3] = UINT32_C(0xc80f0000);
         code[n / 2 * 3 + 2] = UINT32_C(0xc0000100);
         for (repeated = 0; repeated < 2; ++repeated) {
            CHECK(xe_reentry_profile(code, words, pixel, masks, 1024, stats));
            CHECK(stats[2] == n && (stats[3] != 0) == (forward != 0));
            for (i = 0; i < n; ++i) CHECK(masks[i] == 0);
            if (stats[4]) CHECK(stats[0] == 0 && stats[1] == 0);
            record(file, forward ? "forward" : "straight", pixel, n, repeated);
         }
      }
   }
   /* Literal packed destinations r0, r63 and r[aL] must still update the
    * public register-use metadata even when reentry scratch is absent.
    * The ADD reads r0/r1; only the relative destination sets dynamic use. */
   for (pixel = 0; pixel < 2; ++pixel) for (mode = 0; mode < 3; ++mode) {
      memset(code, 0, sizeof(code));
      cf(code, 0, 2 | (1u << 12), 0x1000);
      cf(code, 3, 2 | (1u << 12), 0x2000);
      code[6] = UINT32_C(0xc80f0000) | (mode == 1 ? 63u : mode == 2 ? 64u : 0u);
      code[8] = UINT32_C(0xc0000100);
      CHECK(xe_reentry_profile(code, 9, pixel, masks, 1024, stats));
      CHECK(stats[2] == 4 && stats[3] == 0);
      CHECK(stats[5] == (mode == 1 ? 64u : 2u));
      CHECK(stats[6] == (mode == 2 ? 1u : 0u));
      CHECK(!stats[4] || (!stats[0] && !stats[1]));
      for (i = 0; i < 4; ++i) CHECK(masks[i] == 0);
   }
   CHECK(!fclose(file));
   puts("PASS: 58 actual analysis allocation observations and metadata identities, 6 literal r0/r63/relative-destination cases; straight/forward scratch contract checked where supported; teardown returns tracked live storage to baseline");
   return 0;
}
