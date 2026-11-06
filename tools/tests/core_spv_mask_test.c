/* C89 exhaustive low-16 masks plus full-width ordering against a bit oracle. */
#include <stdio.h>
#include "../../libretro/core_spv_mask.h"
#include "core_spv_memexport_program.h"
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"mask line %u\n",(unsigned)__LINE__); return 1; } } while(0)
static int check_mask(uint32_t mask) {
   uint32_t remaining=mask, index=99;
   unsigned i;
   for(i=0;i<32;++i) if(mask&(UINT32_C(1)<<i)) {
      CHECK(xe_spv_mask_take(&remaining,&index) && index==i);
   }
   index=99; CHECK(!xe_spv_mask_take(&remaining,&index) && index==99 && !remaining);
   return 0;
}
int main(void) {
   uint32_t words[23], mask, expected, j;
   size_t count;
   uint32_t i, random=UINT32_C(0xa947138d);
   for(i=0;i<65536;++i) CHECK(!check_mask(i));
   for(i=0;i<100000;++i) {
      random=random*UINT32_C(1664525)+UINT32_C(1013904223);
      CHECK(!check_mask(random));
   }
   CHECK(!check_mask(UINT32_C(0xffffffff)) && !check_mask(UINT32_C(0x80000000)));
   for(mask=0;mask<32;++mask) {
      words[0]=UINT32_C(0x12345678); words[22]=UINT32_C(0x87654321);
      count=xe_spv_memexport_test_program(words+1,mask);
      expected=0; for(j=0;j<5;++j) expected+=(mask>>j)&1;
      CHECK(count==(mask?6+3*expected:3));
      CHECK(words[0]==UINT32_C(0x12345678) && words[22]==UINT32_C(0x87654321));
      if(mask) CHECK((words[2]&65535)==UINT32_C(0xc600) && (words[4]&63)==32);
   }
   puts("PASS: 65536 exhaustive low masks and 100000 full-width masks, ascending order and empty-output preservation");
   return 0;
}
