#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../libretro/core_spv_u32_set.h"
#include "../../libretro/core_spv_probe.h"
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); return 1; } } while(0)
static size_t allocations, releases;
static int fail_allocation;
static void *allocate(size_t bytes) { if(fail_allocation) return NULL; ++allocations; return malloc(bytes); }
static void release(void *value) { if(value) ++releases; free(value); }
static int compare(const void *a, const void *b) {
   uint32_t x=*(const uint32_t*)a,y=*(const uint32_t*)b;
   return (x>y)-(x<y);
}
int main(void)
{
   xe_spv_u32_set set;
   uint32_t expected[1024], seed=123, probe[XE_SPV_COMPUTE_PROBE_WORDS+2];
   uint32_t *old;
   size_t i, pass, warmed;
   memset(&set,0,sizeof(set));
   CHECK(!xe_spv_u32_set_contains(&set,0));
   for(i=0;i<64;++i) CHECK(xe_spv_u32_set_insert(&set,(uint32_t)i,allocate,release));
   old=set.values; fail_allocation=1;
   CHECK(xe_spv_u32_set_insert(&set,0,allocate,release));
   CHECK(!xe_spv_u32_set_insert(&set,64,allocate,release));
   CHECK(set.count==64 && set.values==old && !xe_spv_u32_set_contains(&set,64));
   CHECK(!xe_spv_u32_set_reserve(&set,(size_t)-1,allocate,release));
   fail_allocation=0;
   for(i=0;i<1024;++i) { seed=seed*UINT32_C(1664525)+UINT32_C(1013904223); expected[i]=seed; }
   expected[0]=0; expected[1]=UINT32_MAX; expected[2]=UINT32_C(0x80000000);
   qsort(expected,1024,sizeof(*expected),compare);
   xe_spv_u32_set_reset(&set);
   for(i=1024;i;--i) CHECK(xe_spv_u32_set_insert(&set,expected[i-1],allocate,release));
   warmed=allocations;
   for(pass=0;pass<100;++pass) {
      xe_spv_u32_set_reset(&set);
      CHECK(!xe_spv_u32_set_view(&set).count);
      for(i=1024;i;--i) CHECK(xe_spv_u32_set_insert(&set,expected[i-1],allocate,release));
      for(i=0;i<1024;++i) CHECK(xe_spv_u32_set_contains(&set,expected[i]));
      CHECK(set.count==1024 && !memcmp(expected,set.values,sizeof(expected)));
      CHECK(xe_spv_u32_set_insert(&set,UINT32_MAX,allocate,release) && set.count==1024);
   }
   CHECK(warmed==allocations);
   xe_spv_u32_set_close(&set,release); CHECK(allocations==releases);
   for(i=0;i<XE_SPV_COMPUTE_PROBE_WORDS+2;++i) probe[i]=UINT32_C(0xfedcba98);
   CHECK(!xe_spv_compute_probe(probe,38,UINT32_C(0x10000),26));
   CHECK(!xe_spv_compute_probe(probe,39,UINT32_C(0x20000),26));
   CHECK(!xe_spv_compute_probe(probe,39,UINT32_C(0x10700),26));
   CHECK(!xe_spv_compute_probe(probe,39,UINT32_C(0x10001),26));
   CHECK(probe[0]==UINT32_C(0xfedcba98));
   CHECK(xe_spv_compute_probe(probe,39,UINT32_C(0x10600),26)==39);
   CHECK(probe[39]==UINT32_C(0xfedcba98) && probe[40]==UINT32_C(0xfedcba98));
   puts("PASS: sorted uint32 extremes, growth failure/retry, 102400 warm inserts, allocation-free probe bounds");
   return 0;
}
