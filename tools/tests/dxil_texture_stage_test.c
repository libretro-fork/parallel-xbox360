/* Native guest 2D fetch policy, driven by real descriptor indices. C89. */
#include "guest_texture_stage.h"
#include "guest_resources.h"
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
static float gamma_decode(float v) {
  float scale = 1.0f/1024, offset = 0, linear, term;
  if (v != v || v < 0) v = 0;
  if (v > 1) v = 1;
  if (v >= 64.0f/255) { scale = 2.0f/1024; offset = -64; }
  if (v >= 96.0f/255) { scale = 4.0f/1024; offset = -256; }
  if (v >= 192.0f/255) { scale = 8.0f/1024; offset = -1024; }
  linear = v*(255.0f*1024); linear *= scale; linear += offset; term = (float)(int)(linear*scale);
  return (linear+term)*(1.0f/1023);
}
static float even(float v) {
  double low = floor((double)v), part = (double)v-low;
  return (float)(low+(part > .5 || (part == .5 && fmod(low,2) != 0)));
}
static float sample(const float *data, unsigned mip, unsigned linear, float u, float v, unsigned component) {
  unsigned size = mip ? 2 : 4, x, y, k, n; float px, py, fx, fy, out = 0, weight;
  int ix, iy;
  data += mip ? 64 : 0;
  if (!linear) { ix = (int)floor((double)(u*(float)size)); iy = (int)floor((double)(v*(float)size));
    x = (unsigned)(ix < 0 ? 0 : ix >= (int)size ? (int)size-1 : ix); y = (unsigned)(iy < 0 ? 0 : iy >= (int)size ? (int)size-1 : iy); return data[(y*size+x)*4+component]; }
  px = u*(float)size-.5f; py = v*(float)size-.5f; ix = (int)floor((double)px); iy = (int)floor((double)py); fx = even((px-(float)ix)*256)/256; fy = even((py-(float)iy)*256)/256;
  for (k = 0; k < 2; ++k) for (n = 0; n < 2; ++n) {
    int tx = ix+(int)n, ty = iy+(int)k;
    x = (unsigned)(tx < 0 ? 0 : tx >= (int)size ? (int)size-1 : tx); y = (unsigned)(ty < 0 ? 0 : ty >= (int)size ? (int)size-1 : ty);
    weight = (n ? fx : 1-fx)*(k ? fy : 1-fy); out += data[(y*size+x)*4+component]*weight;
  }
  return out;
}
int main(int argc, char **argv) {
  XeDxilShaderModule *shader; XeDxilShaderBinary binary; XeDxilAlu alu; XeDxilAluIo io;
  XeDxilTextureLayout layout; XeDxilTextureStageOptions options; XeDxilTextureStage *stage = NULL;
  XeDxilTextureInstruction instruction; XeDxilTextureOperands operands; struct dxil_module *m;
  XeDxilValue constants, output, result[4], value;
  uint32_t words[64], golden[8]; float texture[80], coords[2] = {.125f,.125f}, ddx[2] = {.25f,0}, ddy[2] = {0,.25f}, u, v, lod = 0, bias = 0, exponent = 1, source, half, scale, offset;
  unsigned test, i, j, current_block = 0, sign, signs = 0, int_bits = 1u<<26, linear = 0, level, width = 8, normalized = 0, integer = 0;
  int status = 1;
  if (argc != 3) return 2;
  test = (unsigned)strtoul(argv[2],NULL,10); if (test > 29) return 2;
  memset(&binary,0,sizeof(binary)); memset(&instruction,0,sizeof(instruction)); memset(&operands,0,sizeof(operands)); memset(&layout,0,sizeof(layout)); memset(&options,0,sizeof(options)); memset(&io,0,sizeof(io)); memset(words,0,sizeof(words)); memset(golden,0,sizeof(golden));
  instruction.opcode = 1; instruction.dimension = 1; instruction.constant_index = 1;
  words[0] = 3; words[2] = 2; words[5] = 0; words[8] = 3u|(3u<<13); words[10] = 1u<<6;
  if (test == 1 || test == 18) { linear = 1; instruction.mag_filter = instruction.min_filter = instruction.mip_filter = 1; words[5] = 1; int_bits = 0; coords[0] = coords[1] = .25f; }
  if (test == 2 || test == 5 || test == 26) { instruction.register_lod = 1; lod = 1; coords[0] = coords[1] = .75f; }
  if (test == 3) { words[10] |= 16u<<12; instruction.lod_bias_sixteenths = 8; bias = 1; coords[0] = coords[1] = .75f; }
  if (test == 4 || test == 5 || test == 7 || test == 25) { instruction.unnormalized = 1; coords[0] = 1.5f; coords[1] = 2.5f; }
  if (test == 5) { words[10] = (1u<<2)|(1u<<6); coords[0] = coords[1] = .5f; }
  if (test == 6) { instruction.offset_halves[0] = 1; instruction.offset_halves[1] = -1; coords[0] = coords[1] = .375f; }
  if (test == 7) { instruction.offset_halves[0] = -16; instruction.offset_halves[1] = 15; }
  if (test == 8 || test == 9 || test == 25 || test == 28 || test == 29) { instruction.computed_lod = instruction.register_gradients = 1; bias = 1; ddx[0] = ddy[1] = test == 9 ? .25f : test == 25 ? 2 : .5f; }
  if (test == 9 || test == 28) words[10] |= 1u<<22;
  if (test == 28 || test == 29) { ddx[0] = ddy[1] = 0; ddx[1] = test == 28 ? .25f : 0; ddy[0] = test == 29 ? .25f : 0; if (test == 29) words[10] |= 1u<<27; }
  if (test == 10 || test == 11) { words[9] = (test == 10 ? 2u : 61u)<<13; exponent = test == 10 ? 4 : .125f; }
  if (test == 12 || test == 20) signs = 0x55;
  if (test == 13 || test == 21) signs = 0xe4;
  if (test == 14 || test == 15) signs = 0xaa;
  if (test >= 14 && test <= 21) { integer = 1; normalized = test == 15 || test == 16 || test == 17 || test == 18; width = test == 17 || test == 18 ? 4 : 8; }
  if (test == 22 || test == 23) signs = 0xff;
  if (test == 24) { instruction.computed_lod = 1; instruction.aniso_filter = 7; }
  if (test == 26) { words[10] |= 1008u<<12; instruction.lod_bias_sixteenths = 8; }
  if (test == 27) instruction.mag_filter = instruction.min_filter = instruction.mip_filter = instruction.aniso_filter = 3;
  if (integer) {
    for (i = 0; i < 4; ++i) { sign = (signs>>(i*2))&3; j = sign == 3 ? 0 : (normalized && sign == 0 && width == 8 ? 0 : width-1)|(sign<<4); int_bits |= j<<(i*6); }
    if (normalized) int_bits |= 1u<<24;
  }
  words[16] = signs<<8; words[33] = int_bits;
  for (i = 0; i < 16; ++i) for (j = 0; j < 4; ++j) texture[i*4+j] = (float)((i*13+j*37)%256)/255;
  if (width == 4) for (i = 0; i < 64; ++i) texture[i] = (float)((i*3+1)%16)/15;
  for (i = 0; i < 4; ++i) for (j = 0; j < 4; ++j) texture[64+i*4+j] = signs == 0x55 || signs == 0xe4 ? (float)(-64+(int)i*32+(int)j*8)/127 : (float)(20+i*4+j)/255;
  if (test == 23) { static const uint32_t special[4] = {0x7fc12345,0x7f800000,0xff800000,0xbf000000}; memcpy(texture,special,16); }
  u = coords[0]; v = coords[1];
  for (i = 0; i < 2; ++i) {
    float *coordinate = i ? &v : &u; float size = test == 5 ? 2.0f : 4.0f;
    *coordinate = instruction.unnormalized ? (*coordinate+(float)instruction.offset_halves[i]*.5f)/size : *coordinate+(float)instruction.offset_halves[i]*.5f/size;
    if (!instruction.unnormalized && !linear && (int_bits&(1u<<26))) *coordinate = ((float)floor((double)(*coordinate*size))+.5f)/size;
    else *coordinate += 1.5f/1024/size;
  }
  level = lod+bias >= .5f ? 1 : 0;
  for (i = 0; i < 4; ++i) {
    sign = (signs>>(i*2))&3; source = sample(texture,sign == 1 ? 1 : level,linear,u,v,i);
    if (sign == 2) source = integer && width > 1 ? (source*(float)((1u<<width)-1)-(float)(1u<<(width-1)))/(float)((1u<<(width-1))-1) : 2*source-1;
    else if (sign == 3) source = gamma_decode(source);
    if (integer) {
      j = (int_bits>>(i*6))&63;
      if (normalized) {
        if (!linear && (int_bits&(1u<<26)) && j >= 1 && j <= 15) { half = (float)(2u<<(j&15)); source = even(source*(half-1))*(half+1)/(half*half); }
        if ((j>>4) == 0) source = even(source*65536)/65536;
        if ((j>>4) == 2 && source < -1) source = -1;
      } else { scale = (float)((1u<<((j&15)+1-(((j>>4)-1u)<2u)))-1); offset = (scale < .5f ? scale : .5f)-.5f; source = source*(scale > .5f ? scale : .5f)+offset; if (!linear && (int_bits&(1u<<26))) source = even(source); }
    }
    source *= exponent; memcpy(golden+i,&source,4);
  }
  shader = xe_dxil_shader_create(DXIL_COMPUTE_SHADER); if (!shader) return 1;
  m = xe_dxil_shader_module(shader); xe_dxil_alu_init(&alu,m); io.current_block = &current_block;
  if (!xe_dxil_shader_bindless(shader) || xe_dxil_shader_resource(shader,1,DXIL_RESOURCE_KIND_RAW_BUFFER,0,0,1,0) != 0 ||
      xe_dxil_shader_resource(shader,2,DXIL_RESOURCE_KIND_CBUFFER,31,1,1,sizeof(words)) != 0 || !xe_dxil_texture_layout_add(&layout,&instruction,0)) goto done;
  output = xe_dxil_resource_handle(&alu,1,0,0); constants = xe_dxil_resource_handle(&alu,2,0,1);
  options.layout = &layout; options.system_constants_bytes = sizeof(words); options.signs_word = 16; options.integer_scale_word = 32;
  stage = xe_dxil_texture_stage_create(shader,constants,constants,&options); if (!stage) goto done;
  operands.coordinate_count = 2; operands.result_components = 15; operands.lod = dxil_module_get_float_const(m,lod);
  for (i = 0; i < 2; ++i) { operands.coordinates[i] = dxil_module_get_float_const(m,coords[i]); operands.ddx[i] = dxil_module_get_float_const(m,ddx[i]); operands.ddy[i] = dxil_module_get_float_const(m,ddy[i]); }
  if (!xe_dxil_texture_stage_fetch(stage,&alu,&io,&instruction,&operands,result)) goto done;
  for (i = 0; i < 8; ++i) { value = i < 4 ? dxil_emit_cast(m,DXIL_CAST_BITCAST,dxil_module_get_int_type(m,32),result[i]) : dxil_module_get_int32_const(m,0); if (!xe_dxil_raw_buffer_store(&alu,output,dxil_module_get_int32_const(m,(int32_t)(i*4)),value)) goto done; }
  if (!alu.failed && dxil_emit_ret_void(m) && xe_dxil_shader_finish(shader,&binary) && write_file(argv[1],"",binary.data,binary.size) &&
      write_file(argv[1],".expected",golden,sizeof(golden)) && write_file(argv[1],".texture",texture,sizeof(texture)) && write_file(argv[1],".constants",words,sizeof(words))) status = 0;
 done: xe_dxil_texture_stage_destroy(stage); xe_dxil_shader_binary_free(&binary); xe_dxil_shader_destroy(shader); return status;
}
