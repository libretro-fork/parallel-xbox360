/* Complete raw guest pixel fixtures. MSVC C89; BSD license. */
#include "guest_pixel_stage.h"
#include "guest_vertex_stage.h"
#include "stage_layout.h"
#include <dxil_module.h>
#include <dxil_function.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int vertex(const char *path, const XeDxilGuestPixelOptions *o) {
  XeDxilShaderModule *shader = xe_dxil_shader_create(DXIL_VERTEX_SHADER);
  XeDxilShaderBinary binary;
  struct dxil_module *m;
  const struct dxil_func *load, *store;
  XeDxilValue args[5], position[4], value, half, one;
  FILE *file;
  char *name;
  unsigned i, j;
  int result = 0;
  memset(&binary,0,sizeof(binary)); if (!shader) return 0;
  (void)o;
  m = xe_dxil_shader_module(shader); half = dxil_module_get_float_const(m,.5f); one = dxil_module_get_float_const(m,1);
  if (xe_dxil_shader_signature(shader,0,"POSITION",0,DXIL_SEM_ARBITRARY,0,0,0,4,0) != 0 ||
      xe_dxil_shader_signature(shader,1,"SV_Position",0,DXIL_SEM_POSITION,DXIL_PROG_SEM_POSITION,XE_DXIL_GUEST_POSITION_ROW,0,4,0) != 0 ||
      xe_dxil_shader_signature(shader,1,"TEXCOORD",0,DXIL_SEM_ARBITRARY,0,0,0,4,0) != 1 ||
      xe_dxil_shader_signature(shader,1,"TEXCOORD",1,DXIL_SEM_ARBITRARY,0,1,0,4,0) != 2) goto done;
  load = dxil_get_function(m,"dx.op.loadInput",DXIL_F32); store = dxil_get_function(m,"dx.op.storeOutput",DXIL_F32);
  args[0] = dxil_module_get_int32_const(m,4); args[1] = dxil_module_get_int32_const(m,0); args[2] = args[1];
  args[4] = dxil_module_get_undef(m,dxil_module_get_int_type(m,32));
  for (i = 0; i < 4; ++i) { args[3] = dxil_module_get_int8_const(m,(int8_t)i); position[i] = load ? dxil_emit_call(m,load,args,5) : NULL; if (!position[i]) goto done; }
  for (i = 0; i < 3; ++i) for (j = 0; j < 4; ++j) {
    value = position[j];
    if (i) {
      if (j < 2) value = dxil_emit_binop(m,DXIL_BINOP_MUL,dxil_emit_binop(m,j ? DXIL_BINOP_SUB : DXIL_BINOP_ADD,j ? one : position[j],j ? position[j] : one,0),half,0);
      else value = dxil_module_get_float_const(m,(float)(j+1));
      if (i == 2) value = dxil_emit_binop(m,DXIL_BINOP_ADD,value,dxil_module_get_float_const(m,4),0);
    }
    args[0] = dxil_module_get_int32_const(m,5); args[1] = dxil_module_get_int32_const(m,(int32_t)i); args[2] = dxil_module_get_int32_const(m,0);
    args[3] = dxil_module_get_int8_const(m,(int8_t)j); args[4] = value;
    if (!store || !value || !dxil_emit_call_void(m,store,args,5)) goto done;
  }
  if (!dxil_emit_ret_void(m) || !xe_dxil_shader_finish(shader,&binary)) goto done;
  name = (char *)malloc(strlen(path)+4); if (!name) goto done;
  strcpy(name,path); strcat(name,".vs"); file = fopen(name,"wb"); free(name); if (!file) goto done;
  result = fwrite(binary.data,1,binary.size,file) == binary.size;
  if (fclose(file)) result = 0;
done:
  xe_dxil_shader_binary_free(&binary); xe_dxil_shader_destroy(shader); return result;
}
static int guest_vertex(const char *path, const XeDxilGuestPixelOptions *pixel) {
  static const uint32_t words[21] = {
    0x16001,0x2000,0,
    1u<<12,(38u<<16)|1672,4,
    (50u<<26)|(15u<<16)|(1u<<15)|62,0,0xc2010100,
    (50u<<26)|(15u<<16)|2,0,0x8b010001,
    (50u<<26)|(15u<<16)|(1u<<15),0,0xc2020200,
    (50u<<26)|(15u<<16)|2,0,0x80020200,
    (50u<<26)|(15u<<16)|(1u<<15)|3,0,0xc2020200
  };
  XeDxilGuestVertexOptions o;
  XeDxilShaderBinary *binary;
  char *name;
  FILE *file;
  int result;
  if (pixel->param_point) return 1;
  memset(&o,0,sizeof(o)); o.program.register_count = 3; o.program.float_constant_count = 3;
  o.system_constants_bytes = 256; o.flags_word = 0; o.index_endian_word = 16; o.index_base_word = 17;
  o.ndc_scale_word = 20; o.ndc_offset_word = 24; o.interpolator_mask = pixel->interpolator_mask;
  o.w_not_reciprocal_mask = 1u<<5; o.xy_divided_mask = 1u<<3; o.z_divided_mask = 1u<<4;
  binary = xe_dxil_build_guest_vertex_owned(&o,words,sizeof(words)/4,2); if (!binary) return 0;
  name = (char *)malloc(strlen(path)+10); if (!name) { xe_dxil_shader_binary_delete(binary); return 0; }
  strcpy(name,path); strcat(name,".guest.vs"); file = fopen(name,"wb"); free(name);
  if (!file) { xe_dxil_shader_binary_delete(binary); return 0; }
  result = fwrite(binary->data,1,binary->size,file) == binary->size;
  if (fclose(file)) result = 0;
  xe_dxil_shader_binary_delete(binary); return result;
}
int main(int argc, char **argv) {
  uint32_t words[21] = {0x6001,0,0};
  XeDxilGuestPixelOptions o;
  XeDxilShaderBinary *binary;
  FILE *file;
  static const unsigned factors[10] = {0,1,2,3,6,7,12,13,14,15};
  unsigned mode, variant, i, reg;
  int result;
  if (argc != 3) return 2;
  mode = (unsigned)strtoul(argv[2],NULL,10); if (mode > 475) return 2;
  variant = mode/28; mode %= 28;
  memset(&o,0,sizeof(o)); o.program.register_count = 16; o.program.float_constant_count = 5;
  o.system_constants_bytes = 256; o.flags_word = 0; o.alpha_reference_word = 1; o.alpha_to_mask_word = 2;
  o.color_bias_word = 4; o.blend_constant_word = 8;
  o.alpha_function_shift = 11; o.gamma_mask = 1u<<14; o.msaa_shift = 8;
  o.front_scale_word = 12; o.back_scale_word = 13; o.front_offset_word = 14; o.back_offset_word = 15;
  o.count_coverage = 1; o.counter_word = 3; o.counter_stride_bytes = 16;
  o.color_mask = 15; o.writes_depth = mode >= 14; o.depth_mode = mode%7;
  o.blend_rgb_factor = 1; o.blend_alpha_factor = 1;
  o.param_gen = mode%14 >= 7; o.param_register = 0; o.param_point = 0;
  o.scale_x = 1; o.scale_y = 1; o.polygon_mask = 1u<<6; o.line_mask = 1u<<7;
  if (variant == 1 || variant == 2 || variant == 4) {
    o.param_gen = 0; o.interpolator_mask = variant == 4 ? 9 : 1; o.centroid_mask = o.interpolator_mask;
    o.precise_interpolation = variant == 2;
  }
  if (variant == 3) { o.param_gen = 1; o.param_point = 1; }
  if (variant == 5) o.native_2x = 1;
  if (variant >= 7) { o.blend_rgb_factor = factors[variant-7]; o.blend_alpha_factor = factors[16-variant]; }
  /* Five exports, plus an ALU instruction whose kill condition is c4 == 0. */
  words[1] = 0x2000;
  for (i = 0; i < 5; ++i) {
    reg = i == 4 ? 61 : i;
    words[3+i*3] = (50u<<26)|(15u<<16)|(1u<<15)|reg;
    words[5+i*3] = (i<<16)|(i<<8)|0x02000000;
  }
  /* Export param_gen or a varying as color 1 in the parameter variants. */
  if (o.param_gen || (variant >= 1 && variant <= 4)) words[8] = variant == 4 ? 0xc2030300 : 0xc2000000;
  if (variant == 6) words[4] = (1u<<28)|(1u<<27);
  words[18] = 50u<<26; words[19] = 0; words[20] = (24u<<24)|(4u<<8);
  binary = xe_dxil_build_guest_pixel_owned(&o,words,sizeof(words)/4,2);
  if (!binary) return 1;
  file = fopen(argv[1],"wb"); if (!file) { xe_dxil_shader_binary_delete(binary); return 1; }
  result = fwrite(binary->data,1,binary->size,file) != binary->size;
  if (fclose(file)) result = 1;
  xe_dxil_shader_binary_delete(binary); return result || !vertex(argv[1],&o) || !guest_vertex(argv[1],&o);
}
