#include "libretro/core_texture_lanes.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"texture lanes line %u\n",(unsigned)__LINE__);exit(1);}}while(0)
int main(void)
{
   static const unsigned counts[16]={0,1,1,2,1,2,2,3,1,2,2,3,2,3,3,4};
   xe_texture_lanes lanes;
   uint32_t mask,recovered;
   unsigned i,previous;
   CHECK(!xe_texture_lanes_init(NULL,1));
   for(mask=0;mask<256;++mask) {
      lanes.count=99;
      if(!mask || mask>15) {
         CHECK(!xe_texture_lanes_init(&lanes,mask) && !lanes.count);
         continue;
      }
      CHECK(xe_texture_lanes_init(&lanes,mask) && lanes.count==counts[mask]);
      recovered=0;previous=0;
      for(i=0;i<lanes.count;++i) {
         CHECK(lanes.component[i]<4 && (!i || lanes.component[i]>previous));
         recovered|=UINT32_C(1)<<lanes.component[i];previous=lanes.component[i];
      }
      CHECK(recovered==mask);
   }
   CHECK(!xe_texture_lanes_init(&lanes,UINT32_C(0x80000001)) && !lanes.count);
   puts("PASS: texture lane masks, ascending borrowed indices and invalid inputs");
   return 0;
}
