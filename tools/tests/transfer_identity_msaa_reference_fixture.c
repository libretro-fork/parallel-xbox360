/* Actual legacy numeric MSAA producer; no replacement shader arithmetic.
 * Link the narrow numeric bridge against the requested parent. BSD; C89. */
#include "transfer_uint_reference.h"
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv)
{
 xe_spv_binary binary={0};xe_test_transfer_reference_options options={0};
 unsigned native,view,msaa,source,scale,mode,count=0;char path[4096];FILE *file;int failed,result=1,all_sources;
 if((argc!=2 && argc!=3) || strlen(argv[1])>3000)return 1;
 all_sources=argc==3;
 if(all_sources && strcmp(argv[2],"--all-sources"))return 1;
 for(native=0;native<2;++native)for(view=0;view<4;++view)for(msaa=0;msaa<3;++msaa)
 for(source=all_sources?0:msaa;source<(all_sources?3:msaa+1);++source)
 for(scale=0;scale<3;++scale)for(mode=0;mode<(msaa?2u:1u);++mode) {
  options.native2=native;options.shading=!mode;
  options.source_format=options.dest_format=view;
  options.source_msaa=source;options.dest_msaa=msaa;
  options.scale_x=options.scale_y=1u<<scale;
  if(!xe_test_transfer_reference_build(&binary,&options))goto done;
  if(all_sources)sprintf(path,"%s-n%u-v%u-sm%u-dm%u-s%u-mode%u.spv",argv[1],native,view,source,msaa,1u<<scale,mode);
  else sprintf(path,"%s-n%u-v%u-m%u-s%u-mode%u.spv",argv[1],native,view,msaa,1u<<scale,mode);
  file=fopen(path,"wb");if(!file)goto done;
  failed=fwrite(binary.words,sizeof(*binary.words),binary.count,file)!=binary.count;
  if(fclose(file))failed=1;
  if(failed)goto done;
  ++count;
 }
 if(count!=(all_sources?360u:120u))goto done;
 result=0;printf("PASS: %u legacy numeric sample-policy/shading/scale SPIR-V modules\n",count);
done:
 xe_spv_binary_close(&binary);return result;
}
