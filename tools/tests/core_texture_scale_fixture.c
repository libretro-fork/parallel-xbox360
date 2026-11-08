/* Verify the production conversion emits only live floating-point lanes. C89. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libretro/core_spv_binary.h"
#include "libretro/core_texture_lanes.h"
int xe_texture_scale_fixture(unsigned,unsigned,xe_spv_binary*);
#define CHECK(x) do {if(!(x)){fprintf(stderr,"scale fixture line %u\n",(unsigned)__LINE__);exit(1);}}while(0)
static unsigned widths[65536];
int main(int argc,char **argv)
{
   xe_spv_binary binary={0};xe_texture_lanes lanes;
   unsigned mask,point,op,arithmetic;
   size_t at,n;
   FILE *file;char path[512];
   CHECK(argc==2 && strlen(argv[1])<400);
   for(mask=1;mask<=15;++mask)for(point=0;point<2;++point) {
      CHECK(xe_texture_lanes_init(&lanes,mask));
      CHECK(xe_texture_scale_fixture(mask,point,&binary));
      CHECK(binary.count>=5 && binary.words[3]<65536);
      memset(widths,0,sizeof(widths));arithmetic=0;
      for(at=5;at<binary.count;at+=n) {
         n=binary.words[at]>>16;op=binary.words[at]&65535;
         CHECK(n && n<=binary.count-at);
         if(op==20 || op==21 || op==22) { CHECK(n>=2 && binary.words[at+1]<65536);widths[binary.words[at+1]]=1; }
         if(op==23) { CHECK(n>=4 && binary.words[at+1]<65536);widths[binary.words[at+1]]=binary.words[at+3]; }
         if(op==12 || op==129 || op==131 || op==133 || op==136 || op==142) {
            CHECK(n>=4 && binary.words[at+1]<65536);
            CHECK(widths[binary.words[at+1]]==lanes.count);++arithmetic;
         }
      }
      CHECK(arithmetic>=9);
      sprintf(path,"%s/scale-%u-%u.spv",argv[1],mask,point);
      file=fopen(path,"wb");CHECK(file);
      CHECK(fwrite(binary.words,4,binary.count,file)==binary.count);
      CHECK(!fclose(file));xe_spv_binary_reset(&binary);
   }
   xe_spv_binary_close(&binary);
   puts("PASS:30 production format-conversion modules contain only live FP lanes");
   return 0;
}
