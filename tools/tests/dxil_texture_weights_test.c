/* Fetch-constant texture weight queries. MSVC C89; BSD license. */
#include "guest_texture.h"
#include "guest_resources.h"
#include "shader_module.h"
#include <dxil_module.h>
#include <dxil_function.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
static int write_file(const char *path, const char *suffix, const void *data, size_t bytes) {
  char *name = (char *)malloc(strlen(path)+strlen(suffix)+1); FILE *file; int ok;
  if (!name) return 0;
  strcpy(name,path); strcat(name,suffix); file = fopen(name,"wb"); free(name);
  if (!file) return 0;
  ok = fwrite(data,1,bytes,file) == bytes; if (fclose(file)) ok = 0; return ok;
}
int main(int argc, char **argv) {
  static const unsigned widths[8] = {1,2,3,17,8192,8193,16777215,16777216};
  static const float coordinates[12] = {-3.25f,-.5f,-.125f,0,.0625f,.1234567f,.5f,.999999f,1,2.75f,256.125f,-65536};
  XeDxilShaderModule *shader; XeDxilShaderBinary binary; XeDxilAlu alu; struct dxil_module *m;
  XeDxilTextureInstruction instruction; XeDxilTextureOperands operands; XeDxilValue source, output, constants, values[4], value;
  uint32_t fetch[192], input[64*8], expected[64*8]; unsigned dimensions[32][3], pattern, d, n, i, j, c, size, count; volatile float v;
  int result = 1;
  if (argc != 2 && argc != 3) return 2;
  pattern = argc == 3 ? (unsigned)strtoul(argv[2],NULL,10) : 0; if (pattern > 3) return 2;
  memset(&binary,0,sizeof(binary)); memset(fetch,0,sizeof(fetch)); memset(input,0,sizeof(input)); memset(expected,0,sizeof(expected));
  for (n = 0; n < 32; ++n) {
    d = n/8; i = n%8;
    dimensions[n][0] = d == 0 ? widths[i] : 1+(i*1171)%8192; dimensions[n][1] = 1+(i*593)%8192; dimensions[n][2] = 1+(i*7)%64;
    if (d == 0) fetch[n*6+2] = dimensions[n][0]-1;
    else if (d == 2 && (i&1)) {
      dimensions[n][0] = 1+(i*317)%2048; dimensions[n][1] = 1+(i*193)%2048; dimensions[n][2] = 1+(i*127)%1024;
      fetch[n*6+2] = (dimensions[n][0]-1)|((dimensions[n][1]-1)<<11)|((dimensions[n][2]-1)<<22); fetch[n*6+5] = 2u<<9;
    } else { fetch[n*6+2] = (dimensions[n][0]-1)|((dimensions[n][1]-1)<<13)|((dimensions[n][2]-1)<<26); fetch[n*6+5] = (d == 3 ? 3u : 1u)<<9; }
  }
  shader = xe_dxil_shader_create(DXIL_COMPUTE_SHADER); if (!shader) return 1;
  m = xe_dxil_shader_module(shader); xe_dxil_alu_init(&alu,m);
  if (!xe_dxil_shader_threads(shader,1,1,1) || xe_dxil_shader_resource(shader,1,DXIL_RESOURCE_KIND_RAW_BUFFER,0,0,1,0) != 0 ||
      xe_dxil_shader_resource(shader,0,DXIL_RESOURCE_KIND_RAW_BUFFER,0,0,1,0) != 0 || xe_dxil_shader_resource(shader,2,DXIL_RESOURCE_KIND_CBUFFER,1,4,1,sizeof(fetch)) != 0) goto done;
  source = xe_dxil_resource_handle(&alu,0,0,0); output = xe_dxil_resource_handle(&alu,1,0,0); constants = xe_dxil_resource_handle(&alu,2,0,4);
  for (c = 0; c < 64; ++c) {
    n = c%32; d = n/8; count = d == 0 ? 1 : d == 2 ? 3 : 2;
    memset(&instruction,0,sizeof(instruction)); memset(&operands,0,sizeof(operands));
    instruction.opcode = 19; instruction.constant_index = n; instruction.dimension = d; instruction.unnormalized = (c/32)&1;
    operands.coordinate_count = count; operands.result_components = 15;
    for (j = 0; j < 3; ++j) {
      v = coordinates[(c+pattern*64+j*5)%12]; memcpy(input+c*8+j,(const void *)&v,4);
      value = xe_dxil_raw_buffer_load(&alu,source,dxil_module_get_int32_const(m,(int32_t)((c*8+j)*4)));
      operands.coordinates[j] = value ? dxil_emit_cast(m,DXIL_CAST_BITCAST,dxil_module_get_float_type(m,32),value) : NULL;
      instruction.offset_halves[j] = (int)(pattern*9+j*7)%32-16;
      if (j < count) {
        if (!instruction.unnormalized) { size = dimensions[n][j]; v *= (float)size; }
        v += (float)instruction.offset_halves[j]*.5f-.5f+1.5f/1024; v -= (float)floor((double)v);
        memcpy(expected+c*8+j,(const void *)&v,4);
      }
    }
    if (!xe_dxil_texture_weights(&alu,&instruction,&operands,constants,values)) goto done;
    for (j = 0; j < 8; ++j) {
      value = j < 4 ? dxil_emit_cast(m,DXIL_CAST_BITCAST,dxil_module_get_int_type(m,32),values[j]) : dxil_module_get_int32_const(m,0);
      if (!xe_dxil_raw_buffer_store(&alu,output,dxil_module_get_int32_const(m,(int32_t)((c*8+j)*4)),value)) goto done;
    }
  }
  if (!alu.failed && dxil_emit_ret_void(m) && xe_dxil_shader_finish(shader,&binary) && write_file(argv[1],"",binary.data,binary.size) &&
      write_file(argv[1],".expected",expected,sizeof(expected)) && write_file(argv[1],".memory",input,sizeof(input)) && write_file(argv[1],".constants",fetch,sizeof(fetch))) result = 0;
 done: xe_dxil_shader_binary_free(&binary); xe_dxil_shader_destroy(shader); return result;
}
