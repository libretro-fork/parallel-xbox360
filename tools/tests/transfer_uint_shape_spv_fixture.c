/* Exact prepared-shape programs and ignored-runtime-field identity. BSD; C89. */
#include "libretro/core_transfer_uint_spv.h"
#include "libretro/core_transfer_uint_shape.h"
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv) {
 static const unsigned formats[4]={4,5,14,15};
 static const unsigned scales[3][2]={{1,1},{3,5},{4,4}};
 xe_spv_binary a={0},b={0};xe_transfer_address_key key={0},other;
 xe_transfer_uint_options options={0};xe_transfer_uint_shape shape,saved;
 unsigned global,classes,scale,n,manual,f,sm,dm,cases=0,i;
 char path[4096];FILE *file;int result=1;
 uint32_t forbidden=(UINT32_C(1)<<XE_ADDR_S_WIDE)|(UINT32_C(1)<<XE_ADDR_S_TW)|
  (UINT32_C(1)<<XE_ADDR_D_PITCH)|(UINT32_C(1)<<XE_ADDR_S_PITCH)|
  (UINT32_C(1)<<XE_ADDR_S_PR)|(UINT32_C(1)<<XE_ADDR_DELTA)|
  (UINT32_C(1)<<23)|(UINT32_C(1)<<24)|(UINT32_C(1)<<27);
 if(argc!=2 || strlen(argv[1])>3000)return 1;
 key.source_color=key.dest_color=1;key.source_pitch=key.dest_pitch=1;
 for(global=0;global<3;++global) {
  classes=global?4:1;
  for(scale=0;scale<classes;++scale)for(n=0;n<2;++n)for(manual=0;manual<2;++manual)
  for(f=0;f<4;++f)for(sm=0;sm<3;++sm)for(dm=0;dm<3;++dm) {
   key.source_msaa=sm;key.dest_msaa=dm;key.source_native2=key.dest_native2=n;key.dest_wide=f&1;
   key.source_scale_x=scale&1?1:scales[global][0];key.source_scale_y=scale&1?1:scales[global][1];
   key.dest_scale_x=scale&2?1:scales[global][0];key.dest_scale_y=scale&2?1:scales[global][1];
   options.source_multisampled=sm!=0;options.sample_mode=dm?(manual?XE_TRANSFER_UINT_SAMPLE_CONSTANT:XE_TRANSFER_UINT_SAMPLE_BUILTIN):XE_TRANSFER_UINT_SINGLE_SAMPLE;
   if(!xe_transfer_uint_shape_prepare(&shape,&options,&key,formats[f]) || (shape.mask&forbidden) ||
      !xe_transfer_uint_spv_build_shape(&a,&options,&key,formats[f]))goto done;
   other=key;other.source_wide=1;other.source_pitch=1023;other.dest_pitch=17;other.dest_to_source_base=-2047;
   if(!xe_transfer_uint_spv_build_shape(&b,&options,&other,formats[f]) || a.count!=b.count || memcmp(a.words,b.words,a.count*sizeof(*a.words)))goto done;
   sprintf(path,"%s-%04u.spv",argv[1],cases);file=fopen(path,"wb");if(!file)goto done;
   if(fwrite(a.words,sizeof(*a.words),a.count,file)!=a.count){fclose(file);goto done;}
   if(fclose(file))goto done;
   ++cases;
  }
 }
 memset(&shape,0xa5,sizeof(shape));saved=shape;
 key.dest_msaa=0;key.source_msaa=0;key.dest_wide=0;options.source_multisampled=0;options.sample_mode=XE_TRANSFER_UINT_SINGLE_SAMPLE;
 for(i=0;i<9;++i) {
  other=key;
  if(i==0)other.source_msaa=3;
  if(i==1)other.dest_msaa=3;
  if(i==2)other.source_pitch=0;
  if(i==3)other.source_color=0;
  if(i==4)other.dest_color=0;
  if(i==5)other.dest_wide=1;
  if(i==6)other.source_native2=2;
  if(i==7)other.dest_scale_y=0;
  if(i==8)other.dest_to_source_base=2048;
  if(xe_transfer_uint_shape_prepare(&shape,&options,&other,4) || memcmp(&shape,&saved,sizeof(shape)))goto done;
 }
 if(cases!=1296)goto done;
 printf("PASS: %u shape programs, fixed-field mask, ignored runtime identity and failure preservation\n",cases);result=0;
done:
 if(result)fprintf(stderr,"Shape fixture failed after %u programs\n",cases);
 xe_spv_binary_close(&a);xe_spv_binary_close(&b);return result;
}
