/* Production signed/unsigned sampling CFG and interpolation fixtures. C89. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libretro/core_spv_binary.h"
int xe_texture_sampling_fixture(unsigned,xe_spv_binary*);
#define CHECK(x) do {if(!(x)){fprintf(stderr,"sampling fixture line %u\n",(unsigned)__LINE__);exit(1);}}while(0)
int main(int argc,char **argv)
{
   xe_spv_binary binary={0};
   unsigned which,branches,samples,phis,op;
   size_t at,n;
   FILE *file;
   char path[512];
   CHECK(argc==2 && strlen(argv[1])<400);
   for(which=0;which<8;++which) {
      CHECK(xe_texture_sampling_fixture(which,&binary));
      branches=samples=phis=0;
      for(at=5;at<binary.count;at+=n) {
         n=binary.words[at]>>16;op=binary.words[at]&65535;
         CHECK(n && n<=binary.count-at);
         branches+=op==250;samples+=op==88;phis+=op==245;
      }
      CHECK(branches==((which&1)?1u:2u) && samples==2 && phis==2);
      sprintf(path,"%s/sample-%u.spv",argv[1],which);
      file=fopen(path,"wb");CHECK(file);
      CHECK(fwrite(binary.words,4,binary.count,file)==binary.count);
      CHECK(!fclose(file));
      xe_spv_binary_reset(&binary);
   }
   xe_spv_binary_close(&binary);
   puts("PASS: eight production sampling fixtures; exact branch/sample/phi counts");
   return 0;
}
