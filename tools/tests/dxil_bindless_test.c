/* Direct descriptor-heap sampling with bound buffers. MSVC C89; BSD license. */
#include "shader_module.h"
#include "guest_resources.h"
#include "guest_texture.h"
#include <dxil_module.h>
#include <dxil_function.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int write_file(const char *path, const char *suffix, const void *data, size_t bytes) {
  char *name = (char *)malloc(strlen(path)+strlen(suffix)+1); FILE *file; int ok;
  if (!name) return 0;
  strcpy(name,path); strcat(name,suffix); file = fopen(name,"wb"); free(name);
  if (!file) return 0;
  ok = fwrite(data,1,bytes,file) == bytes; if (fclose(file)) ok = 0; return ok;
}
int main(int argc, char **argv) {
  static const float coords[8][3] = {{.125f,.125f,0},{.25f,.25f,0},{.875f,.875f,0},{.75f,.75f,1},{.25f,.25f,0},{.125f,.125f,0},{.375f,.375f,0},{.25f,.25f,1}};
  static const float expected[8][4] = {{1,1,1,1},{20,21,22,23},{4,4,16,1},{32,33,34,35},{1.5f,1.5f,3.5f,1},{20,21,22,23},{2,2,6,1},{20,21,22,23}};
  XeDxilShaderModule *shader; XeDxilShaderBinary binary; XeDxilAlu alu; XeDxilTextureSample sample;
  struct dxil_module *m; XeDxilValue output, constants, indices, index, handle, values[4], address;
  uint32_t constants_data[16] = {3,0,2,0,1,0,0,0,0,0,0,1,0,0,0,0}, golden[64]; float texture[80]; unsigned i, j, array, pixel; int result = 1;
  if (argc != 2 && argc != 3) return 2;
  array = argc == 3 && !strcmp(argv[2],"array"); pixel = argc == 3 && !strcmp(argv[2],"pixel");
  if (argc == 3 && !array && !pixel) return 2;
  memset(&binary,0,sizeof(binary)); memset(golden,0,sizeof(golden));
  shader = xe_dxil_shader_create(pixel ? DXIL_PIXEL_SHADER : DXIL_COMPUTE_SHADER); if (!shader) return 1;
  m = xe_dxil_shader_module(shader); xe_dxil_alu_init(&alu,m);
  if (!xe_dxil_shader_bindless(shader) || (!pixel && !xe_dxil_shader_threads(shader,1,1,1)) ||
      xe_dxil_shader_resource(shader,1,DXIL_RESOURCE_KIND_RAW_BUFFER,0,0,1,0) != 0 ||
      xe_dxil_shader_resource(shader,2,DXIL_RESOURCE_KIND_CBUFFER,31,1,1,64) != 0 ||
      xe_dxil_shader_resource(shader,0,array ? DXIL_RESOURCE_KIND_TEXTURE2D_ARRAY : DXIL_RESOURCE_KIND_TEXTURE2D,0,0,1,0) != 0 ||
      xe_dxil_shader_resource(shader,0,DXIL_RESOURCE_KIND_RAW_BUFFER,0,3,1,0) != 1 ||
      xe_dxil_shader_resource(shader,3,DXIL_RESOURCE_KIND_SAMPLER,0,0,1,0) != 0) goto done;
  if (pixel && (xe_dxil_shader_barycentrics(shader) < 0 || m->minor_version != 6)) goto done;
  output = xe_dxil_resource_handle(&alu,1,0,0); constants = xe_dxil_resource_handle(&alu,2,0,1);
  indices = xe_dxil_resource_handle(&alu,0,1,3);
  if (!output || !constants || xe_dxil_shader_bound_handle(shader,0,0,1) ||
      xe_dxil_shader_heap_handle(shader,0,2,dxil_module_get_int32_const(m,0),0)) goto done;
  for (i = 0; i < 8; ++i) {
    memset(&sample,0,sizeof(sample)); index = xe_dxil_raw_buffer_load(&alu,indices,dxil_module_get_int32_const(m,(int32_t)((i%4)*8)));
    handle = xe_dxil_cbuffer_word(&alu,constants,dxil_module_get_int32_const(m,12));
    index = index && handle ? dxil_emit_binop(m,DXIL_BINOP_ADD,index,handle,0) : NULL;
    sample.texture = xe_dxil_shader_heap_handle(shader,0,0,index,i&1);
    handle = xe_dxil_raw_buffer_load(&alu,indices,dxil_module_get_int32_const(m,(int32_t)((4+i/4)*8+4)));
    sample.sampler = xe_dxil_shader_heap_handle(shader,3,0,handle,i&1);
    sample.dimension = array ? XE_DXIL_SAMPLE_2D_ARRAY : XE_DXIL_SAMPLE_2D; sample.mode = XE_DXIL_SAMPLE_LEVEL;
    sample.coordinates[2] = dxil_module_get_float_const(m,0);
    sample.coordinates[0] = dxil_module_get_float_const(m,coords[i][0]); sample.coordinates[1] = dxil_module_get_float_const(m,coords[i][1]);
    sample.lod_or_bias = dxil_module_get_float_const(m,coords[i][2]);
    if (!xe_dxil_texture_sample(&alu,&sample,values)) goto done;
    for (j = 0; j < 4; ++j) {
      address = dxil_module_get_int32_const(m,(int32_t)((i*8+j)*4));
      handle = dxil_emit_cast(m,DXIL_CAST_BITCAST,dxil_module_get_int_type(m,32),values[j]);
      if (!xe_dxil_raw_buffer_store(&alu,output,address,handle)) goto done;
      memcpy(&golden[i*8+j],&expected[i][j],4);
    }
  }
  for (i = 0; i < 16; ++i) { texture[i*4] = (float)(i%4+1); texture[i*4+1] = (float)(i/4+1); texture[i*4+2] = (float)(i+1); texture[i*4+3] = 1; }
  for (i = 0; i < 16; ++i) texture[64+i] = (float)(20+i);
  if (!alu.failed && dxil_emit_ret_void(m) && xe_dxil_shader_finish(shader,&binary) &&
      write_file(argv[1],"",binary.data,binary.size) && write_file(argv[1],".expected",golden,sizeof(golden)) &&
      write_file(argv[1],".texture",texture,sizeof(texture)) && write_file(argv[1],".constants",constants_data,sizeof(constants_data))) result = 0;
 done: xe_dxil_shader_binary_free(&binary); xe_dxil_shader_destroy(shader); return result;
}
