/* Large-surface oracle dispatch and the production rectangle vertex shader.
 * Raw SPIR-V only. BSD; MSVC C89. */
#define XE_TRANSFER_MSAA_PROBE_2D
#define main identity_msaa_probe_main
#include "transfer_identity_msaa_spv_fixture.c"
#undef main
#include "libretro/core_transfer_geometry.h"
int main(int argc,char **argv)
{
 xe_spv_binary binary={0};char path[4096];FILE *file;int failed;
 if(identity_msaa_probe_main(argc,argv))return 1;
 if(!xe_transfer_rectangle_spv(&binary))goto fail;
 sprintf(path,"%s-vertex.spv",argv[1]);file=fopen(path,"wb");if(!file)goto fail;
 failed=fwrite(binary.words,4,binary.count,file)!=binary.count;
 if(fclose(file))failed=1;
 if(failed)goto fail;
 xe_spv_binary_close(&binary);return 0;
fail:
 xe_spv_binary_close(&binary);return 1;
}
