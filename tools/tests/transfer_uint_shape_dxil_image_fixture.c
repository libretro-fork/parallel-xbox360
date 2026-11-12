/* Exact six-scale image keys; native shape emission only. BSD; MSVC C89. */
#include "src/xenia/gpu/dxil/transfer_uint.h"
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv) {
 static const unsigned formats[4]={4,5,14,15};
 static const unsigned scales[6][4]={{1,1,1,1},{2,2,2,2},{3,5,3,5},{1,1,3,5},{3,5,1,1},{1,1,1,3}};
 xe_transfer_address_key key={0};xe_transfer_uint_options options={0};
 XeDxilShaderBinary binary={0};unsigned n,c,f,sm,dm,mode,cases=0;
 char path[4096];FILE *file,*list;int failed,result=1;
 if(argc!=2 || strlen(argv[1])>3000)return 1;
 sprintf(path,"%s-shaped.list",argv[1]);list=fopen(path,"wb");if(!list)return 1;
 key.source_color=key.dest_color=1;key.source_pitch=key.dest_pitch=1;
 for(n=0;n<2;++n)for(c=0;c<6;++c)for(f=0;f<4;++f)
 for(sm=0;sm<3;++sm)for(dm=0;dm<3;++dm)for(mode=0;mode<2;++mode) {
  key.source_msaa=sm;key.dest_msaa=dm;key.source_native2=key.dest_native2=n;
  key.dest_scale_x=scales[c][0];key.dest_scale_y=scales[c][1];
  key.source_scale_x=scales[c][2];key.source_scale_y=scales[c][3];key.dest_wide=f&1;
  options.source_multisampled=sm!=0;
  options.sample_mode=dm?(mode?XE_TRANSFER_UINT_SAMPLE_CONSTANT:XE_TRANSFER_UINT_SAMPLE_BUILTIN):XE_TRANSFER_UINT_SINGLE_SAMPLE;
  if(!xe_dxil_transfer_uint_shader_shape(&options,&key,formats[f],&binary))goto done;
  sprintf(path,"%s-shape-n%u-c%u-f%u-s%u-d%u-m%u.dxil",argv[1],n,c,f,sm,dm,mode);
  file=fopen(path,"wb");if(!file)goto done;
  failed=fwrite(binary.data,1,binary.size,file)!=binary.size;
  if(fclose(file))failed=1;
  if(failed || fprintf(list,"%s\n",path)<0)goto done;
  xe_dxil_shader_binary_free(&binary);++cases;
 }
 if(cases!=864)goto done;
 printf("PASS: %u native shaped image interfaces, all sample/scale/destination views\n",cases);result=0;
done:
 xe_dxil_shader_binary_free(&binary);if(fclose(list))result=1;return result;
}
