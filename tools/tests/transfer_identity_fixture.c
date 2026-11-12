/* Common native/raw-SPIR-V interface corpus. BSD; MSVC C89. */
#include "transfer_identity_cases.h"
#ifdef IDENTITY_DXIL
#include "src/xenia/gpu/dxil/transfer_identity.h"
#include "src/xenia/gpu/dxil/transfer_rectangle.h"
typedef XeDxilShaderBinary binary;
#define DATA(x) ((x).data)
#define SIZE(x) ((x).size)
#define CLOSE(x) xe_dxil_shader_binary_free(x)
#define BUILD(out,options,key) xe_dxil_transfer_identity_shader(options,key,out)
#define EXTENSION "dxil"
#define VERTEX(out) xe_dxil_build_transfer_rectangle(out)
#else
#include "libretro/core_transfer_identity_spv.h"
#include "libretro/core_transfer_geometry.h"
typedef xe_spv_binary binary;
#define DATA(x) ((x).words)
#define SIZE(x) ((x).count*sizeof(uint32_t))
#define CLOSE(x) xe_spv_binary_close(x)
#define BUILD(out,options,key) xe_transfer_identity_spv_build(out,options,key)
#define EXTENSION "spv"
#define VERTEX(out) xe_transfer_rectangle_spv(out)
#endif
#include <stdio.h>
int main(int argc,char **argv)
{
 binary base={0},other={0};xe_transfer_address_key key,bad;xe_transfer_identity_options options;
 unsigned n,d,s,c,t,m,v,count=0;char path[4096];FILE *file,*list;int failed;
 if(argc!=2 || strlen(argv[1])>2048)return 1;
 sprintf(path,"%s-%s.list",argv[1],EXTENSION);list=fopen(path,"wb");if(!list)return 1;
 for(n=0;n<2;++n)for(d=0;d<3;++d)for(s=0;s<3;++s)for(c=0;c<4;++c)
 for(t=0;t<4;++t)for(m=0;m<(d?2u:1u);++m) {
  identity_case(&key,&options,n,d,s,c,t,m);
  if(!BUILD(&base,&options,&key))goto fail;
  /* All four actual numeric-view classes share the same module. The PSO
   * still requires its own exact typed attachment format. Compare bytes,
   * not a hash, before permitting initialization-time module reuse. */
  for(v=1;v<4;++v) {
   options.view=v;if(!BUILD(&other,&options,&key))goto fail;
   if(SIZE(base)!=SIZE(other) || memcmp(DATA(base),DATA(other),SIZE(base)))goto fail;
   CLOSE(&other);
  }
  bad=key;bad.dest_wide=1;
  if(BUILD(&other,&options,&bad))goto fail;
  sprintf(path,"%s-n%u-d%u-s%u-c%u-t%u-m%u.%s",argv[1],n,d,s,c,t,m,EXTENSION);
  file=fopen(path,"wb");if(!file)goto fail;
  failed=fwrite(DATA(base),1,SIZE(base),file)!=SIZE(base);
  if(fclose(file))failed=1;
  if(failed || fprintf(list,"%s\n",path)<0)goto fail;
  CLOSE(&base);++count;
 }
 if(!VERTEX(&base))goto fail;
 sprintf(path,"%s-vertex.%s",argv[1],EXTENSION);file=fopen(path,"wb");if(!file)goto fail;
 failed=fwrite(DATA(base),1,SIZE(base),file)!=SIZE(base);
 if(fclose(file))failed=1;
 if(failed || fprintf(list,"%s\n",path)<0)goto fail;
 CLOSE(&base);
 if(fclose(list))return 1;
 printf("PASS: %u %s shapes, all four views byte-identical per shape, invalid width rejected\n",count,EXTENSION);return 0;
fail:CLOSE(&base);CLOSE(&other);fclose(list);return 1;
}
