/* Native volume and stacked-array policies, independent numeric oracle. C89. */
#include "guest_texture_stage.h"
#include "guest_resources.h"
#include <dxil_module.h>
#include <dxil_function.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
static int write_file(const char *path,const char *suffix,const void *data,size_t bytes) {
  char *name = (char *)malloc(strlen(path)+strlen(suffix)+1); FILE *f; int ok;
  if (!name) return 0;
  strcpy(name,path); strcat(name,suffix); f = fopen(name,"wb"); free(name); if (!f) return 0;
  ok = fwrite(data,1,bytes,f) == bytes; if (fclose(f)) ok = 0; return ok;
}
static unsigned clamped(int x,unsigned size) { return x < 0 ? 0 : (unsigned)x >= size ? size-1 : (unsigned)x; }
int main(int argc,char **argv) {
  XeDxilShaderModule *shader; XeDxilShaderBinary binary; XeDxilAlu alu; XeDxilAluIo io;
  XeDxilTextureLayout layout; XeDxilTextureStageOptions options; XeDxilTextureStage *stage = NULL;
  XeDxilTextureInstruction instruction; XeDxilTextureOperands operands; struct dxil_module *m;
  XeDxilValue constants, output, result[4], value;
  uint32_t words[64], golden[8]; float texture[320], coords[3] = {.375f,.625f,.625f}, dx[3] = {0,0,0}, dy[3] = {0,0,0};
  float z, weight = 0, expected, scale = 1; unsigned test, kind, stack, mip, layer, size, depth, x, y, j, a, b, offset = 0, level = 0, current_block = 0, linear = 0;
  int status = 1;
  if (argc != 3) return 2;
  test = (unsigned)strtoul(argv[2],NULL,10); if (test > 31) return 2;
  stack = test >= 16; kind = test%16;
  memset(&binary,0,sizeof(binary)); memset(&instruction,0,sizeof(instruction)); memset(&operands,0,sizeof(operands)); memset(&layout,0,sizeof(layout)); memset(&options,0,sizeof(options)); memset(&io,0,sizeof(io)); memset(words,0,sizeof(words)); memset(golden,0,sizeof(golden));
  instruction.opcode = 1; instruction.dimension = 2; instruction.constant_index = 2;
  words[0] = words[4] = 3; words[2] = words[6] = 2;
  words[14] = stack ? 3u|(3u<<13)|(3u<<26) : 3u|(3u<<11)|(3u<<22); words[16] = 1u<<6; words[17] = (stack ? 1u : 2u)<<9;
  if (kind == 1) { instruction.unnormalized = 1; for(j=0;j<3;++j) coords[j] *= 4; }
  if (kind == 2) instruction.offset_halves[2] = 2;
  if (kind == 3) { instruction.register_lod = 1; level = 1; }
  if (kind == 4) { instruction.computed_lod = instruction.register_gradients = 1; dx[0] = dy[1] = .5f; level = 1; }
  if (kind == 5) { instruction.mag_filter = instruction.min_filter = instruction.mip_filter = 1; words[9] = 1; coords[2] = .5f; instruction.volume_mag_filter = 1; linear = 1; }
  if (kind == 6) { instruction.volume_mag_filter = 3; words[16] |= 1; coords[2] = .5f; linear = stack; }
  if (kind >= 7 && kind <= 10) {
    instruction.computed_lod = instruction.register_gradients = 1; coords[2] = .5f;
    dx[2] = kind == 9 ? .125f : kind == 7 ? 2.0f : -2.0f;
    instruction.volume_mag_filter = kind == 9 ? 1 : 0; instruction.volume_min_filter = kind == 10 ? 3 : kind == 9 ? 0 : 1;
    if (kind == 10) words[16] |= 2;
    linear = stack; level = !stack && kind != 9;
  }
  if (kind == 11) coords[2] = 10;
  if (kind == 12) coords[2] = -10;
  if (kind == 13) { coords[2] = (1.5f-1.5f/1024)/4; instruction.volume_mag_filter = 1; linear = stack; }
  if (kind == 14) { words[32] = 0x55u<<16; level = 1; }
  if (kind == 15) { words[15] = 2u<<13; scale = 4; }
  for(layer=0;layer<(stack?4u:1u);++layer) for(mip=0;mip<2;++mip) {
    size = mip ? 2 : 4; depth = stack ? 1 : size;
    for(a=0;a<depth;++a) for(y=0;y<size;++y) for(x=0;x<size;++x) for(j=0;j<4;++j) texture[offset++] = (float)(mip*1000+(stack?layer:a)*100+y*10+x+j);
  }
  z = instruction.unnormalized ? coords[2] : coords[2]*4;
  z += (float)instruction.offset_halves[2]*.5f+1.5f/1024;
  size = level ? 2 : 4; x = (unsigned)(.375f*(float)size); y = (unsigned)(.625f*(float)size);
  if (stack) {
    if (!(z > 0)) z = 0;
    if (z > 4) z = 4;
    if (linear) z -= .5f;
    a = clamped((int)floor((double)z),4); b = clamped((int)floor((double)z)+1,4);
    weight = linear ? z-(float)floor((double)z) : 0;
  } else {
    z *= (float)size/4;
    if (linear) z -= .5f;
    a = clamped((int)floor((double)z),size); b = clamped((int)floor((double)z)+1,size);
    weight = linear ? .5f : 0;
  }
  for(j=0;j<4;++j) { expected = ((float)(level*1000+a*100+y*10+x+j)+(float)((int)b-(int)a)*100*weight)*scale; memcpy(golden+j,&expected,4); }
  shader = xe_dxil_shader_create(DXIL_COMPUTE_SHADER); if (!shader) return 1;
  m = xe_dxil_shader_module(shader); xe_dxil_alu_init(&alu,m); io.current_block = &current_block;
  if (!xe_dxil_shader_bindless(shader) || xe_dxil_shader_resource(shader,1,DXIL_RESOURCE_KIND_RAW_BUFFER,0,0,1,0)!=0 || xe_dxil_shader_resource(shader,2,DXIL_RESOURCE_KIND_CBUFFER,31,1,1,sizeof(words))!=0 || !xe_dxil_texture_layout_add(&layout,&instruction,0)) goto done;
  output = xe_dxil_resource_handle(&alu,1,0,0); constants = xe_dxil_resource_handle(&alu,2,0,1);
  options.layout = &layout; options.system_constants_bytes = sizeof(words); options.signs_word = 32; options.integer_scale_word = 40;
  stage = xe_dxil_texture_stage_create(shader,constants,constants,&options); if (!stage) goto done;
  operands.coordinate_count = 3; operands.result_components = 15; operands.lod = dxil_module_get_float_const(m,1);
  for(j=0;j<3;++j) { operands.coordinates[j] = dxil_module_get_float_const(m,coords[j]); operands.ddx[j] = dxil_module_get_float_const(m,dx[j]); operands.ddy[j] = dxil_module_get_float_const(m,dy[j]); }
  if (!xe_dxil_texture_stage_fetch(stage,&alu,&io,&instruction,&operands,result)) goto done;
  for(j=0;j<8;++j) { value = j<4 ? dxil_emit_cast(m,DXIL_CAST_BITCAST,dxil_module_get_int_type(m,32),result[j]) : dxil_module_get_int32_const(m,0); if (!xe_dxil_raw_buffer_store(&alu,output,dxil_module_get_int32_const(m,(int32_t)(j*4)),value)) goto done; }
  if (!alu.failed && dxil_emit_ret_void(m) && xe_dxil_shader_finish(shader,&binary) && write_file(argv[1],"",binary.data,binary.size) && write_file(argv[1],".expected",golden,sizeof(golden)) && write_file(argv[1],".texture",texture,offset*sizeof(float)) && write_file(argv[1],".constants",words,sizeof(words))) status = 0;
 done: xe_dxil_texture_stage_destroy(stage); xe_dxil_shader_binary_free(&binary); xe_dxil_shader_destroy(shader); return status;
}