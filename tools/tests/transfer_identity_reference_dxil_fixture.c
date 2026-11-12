/* Actual existing numeric DXIL producer, no replacement shader math.
 * Link against the verified requested revision's native emitter. BSD; C89. */
#include "src/xenia/gpu/dxil/edram_transfer.h"
#include <stdio.h>
#include <string.h>
static int write_module(const char *path,const XeDxilShaderBinary *binary,FILE *list)
{
 FILE *file;int failed;
 if(!binary->data || !binary->size)return 0;
 file=fopen(path,"wb");if(!file)return 0;
 failed=fwrite(binary->data,1,binary->size,file)!=binary->size;
 if(fclose(file))failed=1;
 return !failed && fprintf(list,"%s\n",path)>0;
}
int main(int argc,char **argv)
{
 XeDxilShaderBinary binary={0};XeDxilEdramTransferOptions options={0};
 unsigned view,msaa,scale,count=0;char path[4096];FILE *list;int result=1;
 if(argc!=2 || strlen(argv[1])>3000)return 1;
 sprintf(path,"%s-reference.list",argv[1]);list=fopen(path,"wb");if(!list)return 1;
 options.mode=1;options.msaa_2x_supported=1;
 for(view=0;view<4;++view)for(msaa=0;msaa<3;++msaa)for(scale=0;scale<3;++scale) {
  options.source_format=options.destination_format=view;
  options.source_msaa=options.destination_msaa=msaa;
  options.scale_x=options.scale_y=1u<<scale;
  if(!xe_dxil_build_edram_transfer(&options,&binary))goto done;
  sprintf(path,"%s-v%u-m%u-s%u.dxil",argv[1],view,msaa,1u<<scale);
  if(!write_module(path,&binary,list))goto done;
  xe_dxil_shader_binary_free(&binary);++count;
 }
 options.source_msaa=options.destination_msaa=0;
 options.scale_x=2;options.scale_y=3;
 for(view=0;view<4;++view)for(scale=0;scale<4;++scale) {
  options.source_format=options.destination_format=view;
  options.source_native=scale&1;options.destination_native=scale>>1;
  if(!xe_dxil_build_edram_transfer(&options,&binary))goto done;
  sprintf(path,"%s-ss-v%u-c%u.dxil",argv[1],view,scale);
  if(!write_module(path,&binary,list))goto done;
  xe_dxil_shader_binary_free(&binary);++count;
 }
 printf("PASS: %u legacy numeric DXIL modules, matching1x2x4x samples/scales and asymmetric SS\n",count);
 result=0;
done:
 xe_dxil_shader_binary_free(&binary);if(fclose(list))result=1;return result;
}
