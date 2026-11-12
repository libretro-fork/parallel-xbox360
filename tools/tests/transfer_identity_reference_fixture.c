/* Exact existing numeric-view producer, with no replacement shader math.
 * Build the narrow bridge with XE_TEST_TRANSFER_NUMERIC_REFERENCE against
 * the requested parent source. BSD; MSVC C89. Untimed fixture generation. */
#include "transfer_uint_reference.h"
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv)
{
 static const unsigned scales[3]={1,2,4};
 xe_spv_binary binary={0};xe_test_transfer_reference_options options={0};
 unsigned view,msaa,scale,count=0;char path[4096];FILE *file;int result=1,failed;
 if(argc!=2 || strlen(argv[1])>3000)return 1;
 options.native2=options.shading=1;
 for(view=0;view<4;++view)for(msaa=0;msaa<3;++msaa)for(scale=0;scale<3;++scale) {
  options.source_format=options.dest_format=view;
  options.source_msaa=options.dest_msaa=msaa;
  options.scale_x=options.scale_y=scales[scale];
  if(!xe_test_transfer_reference_build(&binary,&options))goto done;
  if(sprintf(path,"%s-v%u-m%u-s%u.spv",argv[1],view,msaa,scales[scale])<0)goto done;
  file=fopen(path,"wb");if(!file)goto done;
  failed=fwrite(binary.words,sizeof(*binary.words),binary.count,file)!=binary.count;
  if(fclose(file))failed=1;
  if(failed)goto done;
  ++count;
 }
 printf("PASS: %u legacy numeric modules, four typed views/three samples/1x2x4x\n",count);
 options.source_msaa=options.dest_msaa=0;
 options.scale_x=2;options.scale_y=3;
 for(view=0;view<4;++view)for(scale=0;scale<4;++scale) {
  options.source_format=options.dest_format=view;
  options.source_native=scale&1;options.dest_native=scale>>1;
  if(!xe_test_transfer_reference_build(&binary,&options))goto done;
  if(sprintf(path,"%s-ss-v%u-c%u.spv",argv[1],view,scale)<0)goto done;
  file=fopen(path,"wb");if(!file)goto done;
  failed=fwrite(binary.words,sizeof(*binary.words),binary.count,file)!=binary.count;
  if(fclose(file))failed=1;
  if(failed)goto done;
  ++count;
 }
 result=0;printf("PASS: %u total legacy numeric modules including asymmetric SS views\n",count);
done:
 xe_spv_binary_close(&binary);return result;
}
