/* Large-surface physical probe and production rectangle VS. BSD; MSVC C89. */
#define XE_TRANSFER_MSAA_PROBE_2D
#define main identity_msaa_probe_main
#include "transfer_identity_msaa_dxil_fixture.c"
#undef main
#include "src/xenia/gpu/dxil/transfer_rectangle.h"
int main(int argc,char **argv) {
 XeDxilShaderBinary out={0};char path[4096];FILE *file;int failed;
 if(identity_msaa_probe_main(argc,argv))return 1;
 if(!xe_dxil_build_transfer_rectangle(&out))goto fail;
 sprintf(path,"%s-vertex.dxil",argv[1]);file=fopen(path,"wb");if(!file)goto fail;
 failed=fwrite(out.data,1,out.size,file)!=out.size;
 if(fclose(file))failed=1;
 if(failed)goto fail;
 xe_dxil_shader_binary_free(&out);return 0;
fail:
 xe_dxil_shader_binary_free(&out);return 1;
}
