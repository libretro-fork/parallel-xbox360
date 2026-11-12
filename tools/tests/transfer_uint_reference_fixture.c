/* Procedural production-reference modules; MSVC C89. */
#include "transfer_uint_reference.h"
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv) {
 static const unsigned pairs[10][2]={{4,6},{6,4},{5,7},{7,5},{14,4},{4,14},{15,5},{5,15},{4,5},{5,4}};
 static const unsigned scales[3]={1,2,4};
 xe_spv_binary binary={0};xe_test_transfer_reference_options options={0};
 unsigned f,m,s,count=0;char path[4096];FILE *file;int result=1;
 if(argc!=2 || strlen(argv[1])>3000)return 1;
 options.native2=options.shading=1;
 for(f=0;f<10;++f)for(m=0;m<3;++m)for(s=0;s<3;++s) {
  options.source_format=pairs[f][0];options.dest_format=pairs[f][1];
  options.source_msaa=options.dest_msaa=m;
  options.scale_x=options.scale_y=scales[s];
  if(!xe_test_transfer_reference_build(&binary,&options))goto done;
  if(sprintf(path,"%s-p%u-m%u-s%u.spv",argv[1],f,m,scales[s])<0)goto done;
  file=fopen(path,"wb");if(!file)goto done;
  if(fwrite(binary.words,sizeof(*binary.words),binary.count,file)!=binary.count){fclose(file);goto done;}
  if(fclose(file))goto done;
  ++count;
 }
 result=0;printf("PASS: %u reference modules, ten ownership pairs/three samples/1x2x4x\n",count);
done:
 xe_spv_binary_close(&binary);return result;
}