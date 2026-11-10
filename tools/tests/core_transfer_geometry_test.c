/* Compact transfer geometry contracts. MSVC C89; BSD license. */
#include "libretro/core_transfer_geometry.h"
#ifdef XE_TRANSFER_TEST_DXIL
#include "src/xenia/gpu/dxil/transfer_rectangle.h"
#endif
#include <stdio.h>
#include <spirv/unified1/spirv.h>

/* Observe exactly the production shader's output, adding only XFB annotations.
 * No source translation or body rewriting is used by this fixture. */
static int feedback(xe_spv_binary *out,const xe_spv_binary *source) {
  size_t offset;
  uint32_t position=0,main_id=0,v[3];
  int capability=0,mode=0,decoration=0;
  xe_spv_binary_append(out,source->words,5);
  for(offset=5;offset<source->count;) {
    const uint32_t *p=source->words+offset;
    unsigned n=p[0]>>16,op=p[0]&65535;
    if(!n || n>source->count-offset) return 0;
    if(op==SpvOpEntryPoint) main_id=p[2];
    if(op==SpvOpDecorate && n==4 && p[2]==SpvDecorationBuiltIn && p[3]==SpvBuiltInPosition) position=p[1];
    offset+=n;
  }
  if(!position || !main_id) return 0;
  for(offset=5;offset<source->count;) {
    const uint32_t *p=source->words+offset;
    unsigned n=p[0]>>16,op=p[0]&65535;
    if(!capability && op!=SpvOpCapability) {
      v[0]=SpvCapabilityTransformFeedback;
      xe_spv_binary_instruction(out,SpvOpCapability,0,0,v,1); capability=1;
    }
    if(!mode && op==SpvOpDecorate) {
      v[0]=main_id;v[1]=SpvExecutionModeXfb;
      xe_spv_binary_instruction(out,SpvOpExecutionMode,0,0,v,2); mode=1;
    }
    if(!decoration && op==SpvOpTypeVoid) {
      v[0]=position;v[1]=SpvDecorationXfbBuffer;v[2]=0;
      xe_spv_binary_instruction(out,SpvOpDecorate,0,0,v,3);
      v[1]=SpvDecorationOffset;
      xe_spv_binary_instruction(out,SpvOpDecorate,0,0,v,3);
      v[1]=SpvDecorationXfbStride;v[2]=16;
      xe_spv_binary_instruction(out,SpvOpDecorate,0,0,v,3); decoration=1;
    }
    xe_spv_binary_append(out,p,n);offset+=n;
  }
  return !out->failed && capability && mode && decoration;
}

static int geometry(void) {
  static const unsigned vk[6][2] = {{0,1},{0,3},{2,1},{2,1},{0,3},{2,3}};
  static const unsigned d3d[6][2] = {{0,1},{2,1},{0,3},{0,3},{2,1},{2,3}};
  static const uint32_t bits[4] = {0x80000000u,0x3f800001u,0x00000001u,0xbf800001u};
  float coordinates[4], stored[4];
  uint32_t bytes = 17;
  unsigned i;
  memcpy(coordinates,bits,sizeof(bits));
  xe_transfer_rectangle_store(stored,coordinates[0],coordinates[1],coordinates[2],coordinates[3]);
  if (memcmp(stored,bits,sizeof(bits))) return 0;
  for (i=0;i<6;++i) {
    if (2u*((XE_TRANSFER_VK_X_MASK>>i)&1)!=vk[i][0] ||
        1+2u*((XE_TRANSFER_VK_Y_MASK>>i)&1)!=vk[i][1] ||
        2u*((XE_TRANSFER_D3D_X_MASK>>i)&1)!=d3d[i][0] ||
        1+2u*((XE_TRANSFER_D3D_Y_MASK>>i)&1)!=d3d[i][1]) return 0;
  }
  if (xe_transfer_rectangle_bytes(UINT32_MAX/16+1,&bytes) || bytes!=17 ||
      xe_transfer_rectangle_bytes(1,NULL)) return 0;
  if (!xe_transfer_rectangle_bytes(0,&bytes) || bytes ||
      !xe_transfer_rectangle_bytes(257,&bytes) || bytes!=4112 ||
      !xe_transfer_rectangle_bytes(UINT32_MAX/16,&bytes) || bytes!=0xfffffff0u) return 0;
  return 1;
}

int main(int argc,char **argv) {
  xe_spv_binary spv = {0};
  xe_spv_binary xfb = {0};
  char path[512];
  uint32_t *words;
  size_t count;
  FILE *file;
  int failed;
#ifdef XE_TRANSFER_TEST_DXIL
  XeDxilShaderBinary dxil = {0};
#endif
  if (argc!=2 || strlen(argv[1])>490 || !geometry() || xe_transfer_rectangle_spv(NULL)) return 1;
  if (!xe_transfer_rectangle_spv(&spv)) return 1;
  words=spv.words; count=spv.count;
  if (!xe_transfer_rectangle_spv(&spv) || spv.words!=words || spv.count!=count) return 1;
  file=fopen(argv[1],"wb");
  if (!file) return 1;
  failed=fwrite(spv.words,sizeof(uint32_t),spv.count,file)!=spv.count;
  if (fclose(file)) failed=1;
  if (!feedback(&xfb,&spv)) return 1;
  sprintf(path,"%s.xfb.spv",argv[1]);
  file=fopen(path,"wb"); if(!file) return 1;
  if(fwrite(xfb.words,sizeof(uint32_t),xfb.count,file)!=xfb.count) failed=1;
  if(fclose(file)) failed=1;
  xe_spv_binary_close(&xfb);
  xe_spv_binary_close(&spv);
#ifdef XE_TRANSFER_TEST_DXIL
  if (xe_dxil_build_transfer_rectangle(NULL) || !xe_dxil_build_transfer_rectangle(&dxil)) return 1;
  sprintf(path,"%s.dxil",argv[1]);
  file=fopen(path,"wb");
  if (!file) return 1;
  if (fwrite(dxil.data,1,dxil.size,file)!=dxil.size) failed=1;
  if (fclose(file)) failed=1;
  xe_dxil_shader_binary_free(&dxil);
#endif
  if (!failed) puts("PASS: rectangle corner order, exact coordinate bits, overflow and SPIR-V storage reuse");
  return failed;
}

