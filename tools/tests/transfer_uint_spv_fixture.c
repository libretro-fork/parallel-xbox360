/* All prepared unsigned image/sample interface variants. BSD; C89. */
#include "libretro/core_transfer_uint_spv.h"
#include "libretro/core_transfer_geometry.h"
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv) {
 xe_spv_binary code={0};xe_transfer_uint_options options;
 char path[4096];FILE *file;unsigned s,m,t,count=0;int failed;
 if(argc!=2 || strlen(argv[1])>2048)return 1;
 for(s=0;s<2;++s)for(m=0;m<3;++m)for(t=0;t<4;++t) {
  options.source_multisampled=s;options.sample_mode=m;options.target=t;
  if(!xe_transfer_uint_spv_build(&code,&options))goto fail;
  sprintf(path,"%s-s%u-m%u-t%u.spv",argv[1],s,m,t);
  file=fopen(path,"wb");if(!file)goto fail;
  failed=fwrite(code.words,4,code.count,file)!=code.count;
  if(fclose(file))failed=1;
  if(failed)goto fail;
  ++count;
 }
 if(!xe_transfer_rectangle_spv(&code))goto fail;
 sprintf(path,"%s-vertex.spv",argv[1]);file=fopen(path,"wb");if(!file)goto fail;
 failed=fwrite(code.words,4,code.count,file)!=code.count;
 if(fclose(file))failed=1;
 if(failed)goto fail;
 printf("PASS: %u raw integer transfer interface variants\n",count);
 xe_spv_binary_close(&code);return 0;
fail:xe_spv_binary_close(&code);return 1;
}
