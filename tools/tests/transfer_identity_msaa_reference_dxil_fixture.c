/* Actual legacy native numeric MSAA producer. BSD; MSVC C89.
 * D3D12 uses sample shading, so no Vulkan manual-sample recipe is generated. */
#include "src/xenia/gpu/dxil/edram_transfer.h"
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv)
{
 XeDxilShaderBinary binary={0};XeDxilEdramTransferOptions options={0};
 unsigned native,view,msaa,source,scale,count=0;char path[4096];FILE *file,*list;
 int failed,result=1,all_sources;
 if((argc!=2 && argc!=3) || strlen(argv[1])>3000)return 1;
 all_sources=argc==3;
 if(all_sources && strcmp(argv[2],"--all-sources"))return 1;
 sprintf(path,"%s-reference.list",argv[1]);list=fopen(path,"wb");if(!list)return 1;
 options.mode=1;
 for(native=0;native<2;++native)for(view=0;view<4;++view)for(msaa=0;msaa<3;++msaa)
 for(source=all_sources?0:msaa;source<(all_sources?3:msaa+1);++source)
 for(scale=0;scale<3;++scale) {
  options.msaa_2x_supported=native;
  options.source_format=options.destination_format=view;
  options.source_msaa=source;options.destination_msaa=msaa;
  options.scale_x=options.scale_y=1u<<scale;
  if(!xe_dxil_build_edram_transfer(&options,&binary))goto done;
  if(all_sources)sprintf(path,"%s-n%u-v%u-sm%u-dm%u-s%u-mode0.dxil",argv[1],native,view,source,msaa,1u<<scale);
  else sprintf(path,"%s-n%u-v%u-m%u-s%u-mode0.dxil",argv[1],native,view,msaa,1u<<scale);
  file=fopen(path,"wb");if(!file)goto done;
  failed=fwrite(binary.data,1,binary.size,file)!=binary.size;
  if(fclose(file))failed=1;
  if(failed || fprintf(list,"%s\n",path)<0)goto done;
  xe_dxil_shader_binary_free(&binary);++count;
 }
 if(count!=(all_sources?216u:72u))goto done;
 result=0;printf("PASS: %u legacy numeric native/emulated MSAA DXIL modules\n",count);
done:
 xe_dxil_shader_binary_free(&binary);if(fclose(list))result=1;return result;
}
