/* All native integer image/sample interface variants. BSD; C89. */
#include "src/xenia/gpu/dxil/transfer_uint.h"
#include "src/xenia/gpu/dxil/transfer_rectangle.h"
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv) {
 XeDxilShaderBinary binary={0};xe_transfer_uint_options options;
 char path[4096];FILE *file,*list;unsigned s,m,t,count=0;int failed;
 if(argc!=2 || strlen(argv[1])>2048)return 1;
 sprintf(path,"%s.list",argv[1]);list=fopen(path,"wb");if(!list)return 1;
 for(s=0;s<2;++s)for(m=0;m<3;++m)for(t=0;t<4;++t) {
  options.source_multisampled=s;options.sample_mode=m;options.target=t;
  if(!xe_dxil_transfer_uint_shader(&options,&binary))goto fail;
  sprintf(path,"%s-s%u-m%u-t%u.dxil",argv[1],s,m,t);
  file=fopen(path,"wb");if(!file)goto fail;
  failed=fwrite(binary.data,1,binary.size,file)!=binary.size;
  if(fclose(file))failed=1;
  if(failed || fprintf(list,"%s\n",path)<0)goto fail;
  xe_dxil_shader_binary_free(&binary);++count;
 }
 if(!xe_dxil_build_transfer_rectangle(&binary))goto fail;
 sprintf(path,"%s-vertex.dxil",argv[1]);file=fopen(path,"wb");if(!file)goto fail;
 failed=fwrite(binary.data,1,binary.size,file)!=binary.size;
 if(fclose(file))failed=1;
 if(failed || fprintf(list,"%s\n",path)<0)goto fail;
 xe_dxil_shader_binary_free(&binary);
 if(fclose(list))return 1;
 printf("PASS: %u native integer transfer interface variants\n",count);return 0;
fail:xe_dxil_shader_binary_free(&binary);fclose(list);return 1;
}
