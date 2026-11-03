/* Native cube fetch policy: all faces, offsets, signed views and LOD. C89. */
#include "guest_texture_stage.h"
#include "guest_resources.h"
#include <dxil_module.h>
#include <dxil_function.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int write_file(const char *path, const char *suffix, const void *data, size_t bytes) {
  char *name = (char *)malloc(strlen(path)+strlen(suffix)+1); FILE *f; int ok;
  if (!name) return 0;
  strcpy(name,path); strcat(name,suffix); f = fopen(name,"wb"); free(name); if (!f) return 0;
  ok = fwrite(data,1,bytes,f) == bytes; if (fclose(f)) ok = 0; return ok;
}
int main(int argc, char **argv) {
  XeDxilShaderModule *shader; XeDxilShaderBinary binary; XeDxilAlu alu; XeDxilAluIo io;
  XeDxilTextureLayout layout; XeDxilTextureStageOptions options; XeDxilTextureStage *stage = NULL;
  XeDxilTextureInstruction instruction; XeDxilTextureOperands operands; struct dxil_module *m;
  XeDxilValue constants, output, result[4], array_result[4], value;
  uint32_t words[64], golden[8]; float texture[480], coords[3] = {1.125f,1.875f,0}, expected;
  unsigned test, face, mip, x, y, j, size, current_block = 0, offset = 0, level = 0, tx = 0, ty = 3;
  int status = 1;
  if (argc != 3) return 2;
  test = (unsigned)strtoul(argv[2],NULL,10); if (test > 19) return 2;
  memset(&binary,0,sizeof(binary)); memset(&instruction,0,sizeof(instruction)); memset(&operands,0,sizeof(operands));
  memset(&layout,0,sizeof(layout)); memset(&options,0,sizeof(options)); memset(&io,0,sizeof(io)); memset(words,0,sizeof(words)); memset(golden,0,sizeof(golden));
  instruction.opcode = 1; instruction.dimension = 3; instruction.constant_index = 1;
  face = test < 6 ? test : test >= 13 && test <= 18 ? test-13 : 2; coords[2] = (float)face;
  if (test == 6) { coords[2] = -10; face = 0; }
  if (test == 7) { coords[2] = 10; face = 5; }
  if (test == 8) { instruction.offset_halves[2] = 2; ++face; }
  if (test == 9) { instruction.offset_halves[0] = 1; tx = 1; }
  if (test == 10) { instruction.unnormalized = 1; coords[0] *= 4; coords[1] *= 4; }
  if (test == 11) { words[16] = 0x5500; level = 1; ty = 1; }
  if (test == 12) { instruction.register_lod = 1; level = 1; ty = 1; }
  if (test >= 13 && test <= 18) { instruction.computed_lod = instruction.register_gradients = 1; level = 1; ty = 1; }
  words[0] = 3; words[2] = 2; words[8] = 3u|(3u<<13); words[10] = 1u<<6;
  for (x = 0; x < 6; ++x) for (mip = 0; mip < 2; ++mip) {
    size = mip ? 2 : 4;
    for (y = 0; y < size; ++y) for (j = 0; j < size*4; ++j) texture[offset++] = (float)(x*1000+mip*100+y*size*4+j);
  }
  if (test == 19) { words[0] = 0; words[2] = 1; words[4] = 3; words[6] = 2; }
  for (j = 0; j < 4; ++j) { expected = (float)(face*1000+level*100+(ty*(level ? 2 : 4)+tx)*4+j); if (test == 19) expected += (float)(48+j); memcpy(golden+j,&expected,4); }
  shader = xe_dxil_shader_create(DXIL_COMPUTE_SHADER); if (!shader) return 1;
  m = xe_dxil_shader_module(shader); xe_dxil_alu_init(&alu,m); io.current_block = &current_block;
  if (test == 19) { instruction.dimension = 1; if (!xe_dxil_texture_layout_add(&layout,&instruction,0)) goto done; instruction.dimension = 3; }
  if (!xe_dxil_shader_bindless(shader) || xe_dxil_shader_resource(shader,1,DXIL_RESOURCE_KIND_RAW_BUFFER,0,0,1,0) != 0 ||
      xe_dxil_shader_resource(shader,2,DXIL_RESOURCE_KIND_CBUFFER,31,1,1,sizeof(words)) != 0 || !xe_dxil_texture_layout_add(&layout,&instruction,0)) goto done;
  output = xe_dxil_resource_handle(&alu,1,0,0); constants = xe_dxil_resource_handle(&alu,2,0,1);
  options.layout = &layout; options.system_constants_bytes = sizeof(words); options.signs_word = 16; options.integer_scale_word = 32;
  stage = xe_dxil_texture_stage_create(shader,constants,constants,&options); if (!stage) goto done;
  operands.coordinate_count = 3; operands.result_components = 15; operands.lod = dxil_module_get_float_const(m,1);
  for (j = 0; j < 3; ++j) { operands.coordinates[j] = dxil_module_get_float_const(m,coords[j]); operands.ddx[j] = dxil_module_get_float_const(m,j == 0 ? 8.0f : 0.0f); operands.ddy[j] = dxil_module_get_float_const(m,j == 1 ? 8.0f : 0.0f); }
  if (test == 19) {
    instruction.dimension = 1; operands.coordinate_count = 2;
    operands.coordinates[0] = dxil_module_get_float_const(m,.125f); operands.coordinates[1] = dxil_module_get_float_const(m,.875f);
    if (!xe_dxil_texture_stage_fetch(stage,&alu,&io,&instruction,&operands,array_result)) goto done;
    instruction.dimension = 3; operands.coordinate_count = 3;
    operands.coordinates[0] = dxil_module_get_float_const(m,coords[0]); operands.coordinates[1] = dxil_module_get_float_const(m,coords[1]);
  }
  if (!xe_dxil_texture_stage_fetch(stage,&alu,&io,&instruction,&operands,result)) goto done;
  if (test == 19) for (j = 0; j < 4; ++j) result[j] = dxil_emit_binop(m,DXIL_BINOP_ADD,result[j],array_result[j],0);
  for (j = 0; j < 8; ++j) { value = j < 4 ? dxil_emit_cast(m,DXIL_CAST_BITCAST,dxil_module_get_int_type(m,32),result[j]) : dxil_module_get_int32_const(m,0); if (!xe_dxil_raw_buffer_store(&alu,output,dxil_module_get_int32_const(m,(int32_t)(j*4)),value)) goto done; }
  if (!alu.failed && dxil_emit_ret_void(m) && xe_dxil_shader_finish(shader,&binary) && write_file(argv[1],"",binary.data,binary.size) &&
      write_file(argv[1],".expected",golden,sizeof(golden)) && write_file(argv[1],".texture",texture,sizeof(texture)) && write_file(argv[1],".constants",words,sizeof(words))) status = 0;
 done: xe_dxil_texture_stage_destroy(stage); xe_dxil_shader_binary_free(&binary); xe_dxil_shader_destroy(shader); return status;
}