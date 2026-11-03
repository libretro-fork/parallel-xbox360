/* Independent export-format / bounds fixture. MSVC C89; BSD license. */
#include "guest_memexport.h"
#include "guest_program.h"
#include "guest_vertex_stage.h"
#include "guest_pixel_stage.h"
#include "shader_module.h"
#include <dxil_module.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct Format { unsigned format, bytes, widths[4], kind; } Format;
static const Format formats[] = {
  {2,1,{8,0,0,0},0},{3,2,{5,5,5,1},0},{4,2,{5,6,5,0},0},{5,2,{5,5,6,0},0},
  {6,4,{8,8,8,8},0},{7,4,{10,10,10,2},0},{8,1,{8,0,0,0},0},{9,1,{8,0,0,0},0},
  {10,2,{8,8,0,0},0},{14,4,{8,8,8,8},0},{15,2,{4,4,4,4},0},
  {16,4,{11,11,10,0},0},{17,4,{10,11,11,0},0},{24,2,{16,0,0,0},0},
  {25,4,{16,16,0,0},0},{26,8,{16,16,16,16},0},{30,2,{16,0,0,0},1},
  {31,4,{16,16,0,0},1},{32,8,{16,16,16,16},1},{36,4,{32,0,0,0},2},
  {37,8,{32,32,0,0},2},{38,16,{32,32,32,32},2},{50,4,{8,8,8,8},0},
  {54,4,{10,10,10,2},0},{55,4,{11,11,10,0},0},{56,4,{10,11,11,0},0}
};
static uint32_t bits(float v) { uint32_t b; memcpy(&b,&v,4); return b; }
static float number(uint32_t b) { float v; memcpy(&v,&b,4); return v; }
static unsigned half(float v) {
  double magnitude = fabs((double)v), scaled, fraction; int exponent = 0; unsigned n, sign = bits(v)>>31;
  if (v != v) magnitude = 0;
  if (magnitude > 131008) magnitude = 131008;
  if (magnitude < ldexp(1.0,-14)) scaled = ldexp(magnitude,24);
  else { frexp(magnitude,&exponent); scaled = ldexp(magnitude,11-exponent); }
  n = (unsigned)floor(scaled); fraction = scaled-floor(scaled);
  n += fraction > .5 || (fraction == .5 && (n&1));
  if (magnitude >= ldexp(1.0,-14)) n += (unsigned)(exponent+13)*1024;
  return (sign<<15)|n;
}
static uint32_t endian32(uint32_t x, unsigned e) {
  if (e == 1 || e == 2) x = ((x&0x00ff00ff)<<8)|((x&0xff00ff00)>>8);
  if (e == 2 || e == 3) x = (x<<16)|(x>>16);
  return x;
}
static int write_file(const char *base, const char *suffix, const void *data, size_t size) {
  char path[2048]; FILE *file; int result;
  if (strlen(base)+strlen(suffix) >= sizeof(path)) return 0;
  strcpy(path,base); strcat(path,suffix); file = fopen(path,"wb"); if (!file) return 0;
  result = fwrite(data,1,size,file) == size; return fclose(file) == 0 && result;
}
static void reference(unsigned config, uint32_t constants[64], uint32_t expected[32]) {
  static const float normalized[6] = {-1,-.5f,0,.5f,1,1.5f};
  static const float floating[8] = {-131008,65536,1.00048828125f,0.0000000298023223876953125f,-0.0f,131008,70000,0.333251953125f};
  const Format *f = NULL; unsigned format = config&63, s = (config>>6)&1, integer = (config>>7)&1, endian = (config>>8)&7, swap = (config>>11)&1, policy = config>>12;
  unsigned i, j, width, offset, byte, order, count = 6, index = 1, dirty = 31, base = 4, valid = 1;
  uint32_t packed[4], out[4], raw, value, mask; float input[5][4], v, multiplier; unsigned char *bytes = (unsigned char *)expected;
  memset(constants,0,256); for (i = 0; i < 32; ++i) expected[i] = 0xa5c39e71;
  for (i = 0; i < sizeof(formats)/sizeof(formats[0]); ++i) if (formats[i].format == format) f = &formats[i];
  constants[0] = 0x40000001; constants[2] = 0x4b000000|(format<<8)|(s<<16)|(integer<<17)|(swap<<19)|endian;
  if (policy == 1) dirty = 21;
  if (policy == 2) count = 3;
  if (policy == 3) index = count;
  if (policy >= 4 && policy <= 7) { constants[policy-4] ^= 0x80000000; valid = 0; }
  if (policy == 8) valid = 0;
  if (policy == 9) { constants[2] |= 63<<8; valid = 0; }
  if (policy == 10) constants[0] |= 0x08000000;
  if (policy == 11) count = 0;
  if (policy == 12) { index = 2; count = 3; }
  if (policy == 13) dirty = 0;
  if (policy == 14) dirty = 3;
  constants[1] ^= 0x4b000000|index; constants[3] ^= 0x4b000000|count;
  constants[24] = dirty; constants[25] = policy != 8;
  for (i = 0; i < 5; ++i) for (j = 0; j < 4; ++j) {
    width = f && f->widths[j] ? f->widths[j] : 8;
    v = f && f->kind ? floating[(i+j)%8] : normalized[(i+j)%6];
    if (f && !f->kind && !s && v < 0) v = -v;
    if (f && !f->kind && integer) {
      multiplier = (float)((1u<<(width-s))-1);
      v = v <= -1 ? -multiplier : v >= 1 ? multiplier : v*multiplier;
    }
    if (i == 4 && j == 2) v = number(0x7fc12345);
    if (i == 4 && j == 3 && f && f->kind) v = number(0x7f800000);
    input[i][j] = v; constants[4+i*4+j] = bits(v);
  }
  if (!f || !valid) return;
  for (i = 0; i < 5; ++i) if ((dirty&(1u<<i)) && index+i < count) {
    for (j = 0; j < 4; ++j) packed[j] = 0;
    offset = 0;
    for (j = 0; j < 4; ++j) {
      v = input[i][swap && (j == 0 || j == 2) ? 2-j : j]; width = f->widths[j];
      if (f->kind == 2) { packed[j] = bits(v); continue; }
      if (!width) continue;
      if (f->kind == 1) raw = half(v);
      else {
        if (v != v) v = 0;
        if (!integer) {
          if (v < (s ? -1 : 0)) v = s ? -1.0f : 0.0f;
          if (v > 1) v = 1;
          v *= (float)((1u<<(width-s))-1);
        }
        raw = v < 0 ? (uint32_t)(int32_t)ceil((double)v-.5) : (uint32_t)floor((double)v+.5);
      }
      /* The first packed component retains its sign extension; later lanes
       * replace their exact fields, including the second 16-bit word pair. */
      if (!(offset%32)) packed[offset/32] = raw;
      else { mask = ((1u<<width)-1)<<(offset%32); packed[offset/32] = (packed[offset/32]&~mask)|((raw<<(offset%32))&mask); }
      offset += width;
    }
    for (j = 0; j < 4; ++j) {
      order = endian == 4 ? j^1 : endian == 5 ? 3-j : j;
      out[j] = endian32(packed[order],endian >= 4 && endian <= 5 ? 2 : endian);
    }
    for (byte = 0; byte < f->bytes; ++byte) {
      value = out[byte/4]>>((byte%4)*8);
      bytes[base+(index+i)*f->bytes+byte] = (unsigned char)value;
    }
  }
}
static int killed(void *context, XeDxilAlu *alu, XeDxilAluIo *io, XeDxilValue condition) {
  (void)context; (void)alu; (void)io; return condition != NULL;
}
static unsigned guest_words(uint32_t words[36], unsigned variant, unsigned vertex) {
  unsigned count = 0, slot, destination, source, i, remaining;
  memset(words,0,36*sizeof(*words));
  if (variant == 8) { words[1] = 0xc600; words[2] = 0x20000000; return 6; }
  /* Two clauses with an intervening ALLOC in variants 4/5. */
  for (i = 0; i < 6; ++i) {
    if (i == 3 && (variant == 2 || variant == 3)) {
      slot = 6+(count++)*3; words[slot] = 50u<<26;
      words[slot+2] = (variant == 2 ? 24u : 27u)<<24; /* eq/ne c0,c0 */
    }
    if (i == 3 && variant == 5) {
      slot = 6+(count++)*3; words[slot] = (50u<<26)|(15u<<16)|(1u<<15)|32;
      words[slot+2] = 2u<<24;
    }
    slot = 6+(count++)*3; destination = 32+i; source = i;
    words[slot] = (50u<<26)|(15u<<16)|(1u<<15)|destination;
    words[slot+2] = (2u<<24)|(source<<16)|(source<<8);
    if (variant == 1 && (i == 2 || i == 4)) words[slot+1] = (1u<<28)|(1u<<27);
  }
  if (vertex) {
    slot = 6+(count++)*3; words[slot] = (50u<<26)|(15u<<16)|(1u<<15)|62;
    words[slot+2] = (2u<<24)|(7u<<16)|(7u<<8);
  }
  words[0] = 0x3002; words[1] = 0x1000;
  if (variant == 4 || variant == 5) words[2] = 0xc6000000;
  remaining = count-3; words[3] = (remaining<<12)|5; words[4] = 0x2000;
  return 6+count*3;
}
static int guest_program(struct dxil_module *m, XeDxilValue cbv, XeDxilValue output,
    XeDxilValue allowed, unsigned variant) {
  uint32_t words[36]; XeDxilGuestProgramOptions options; XeDxilGuestProgram *program; unsigned count; int result;
  memset(&options,0,sizeof(options));
  options.float_constant_count = 6; options.float_constants = cbv; options.memexport_mask = 31;
  if (variant == 8) options.memexport_mask = 0;
  options.memexport_uav = output; options.memexport_allowed = allowed; options.kill = killed;
  count = guest_words(words,variant,0);
  program = xe_dxil_guest_program_create(m,&options); if (!program) return 0;
  result = xe_dxil_guest_program_emit(program,words,count,4);
  xe_dxil_guest_program_destroy(program); return result;
}
static int stages(const char *path, unsigned pixel, unsigned variant) {
  XeDxilGuestVertexOptions vs; XeDxilGuestPixelOptions ps; XeDxilGuestRopOptions rop; XeDxilShaderBinary *binary;
  uint32_t words[36], system[64]; unsigned count; int result;
  memset(&vs,0,sizeof(vs)); memset(&ps,0,sizeof(ps)); memset(system,0,sizeof(system));
  system[0] = 32; system[4] = system[5] = system[6] = bits(1);
  vs.program.float_constant_count = 8; vs.program.memexport_mask = pixel ? 0 : 31;
  vs.program.kill = killed; vs.system_constants_bytes = 256; vs.flags_word = 0;
  vs.ndc_scale_word = 4; vs.ndc_offset_word = 8; vs.index_endian_word = 1; vs.index_base_word = 2; vs.w_not_reciprocal_mask = 32;
  count = guest_words(words,variant,1);
  if (pixel) {
    memset(words,0,sizeof(words)); words[0] = 0x1001; words[1] = 0x2000;
    words[3] = (50u<<26)|(15u<<16)|(1u<<15)|62; words[5] = (2u<<24)|(7u<<16)|(7u<<8); count = 6;
  }
  binary = xe_dxil_build_guest_vertex_owned(&vs,words,count,pixel ? 2 : 4); if (!binary) return 0;
  result = write_file(path,".vs",binary->data,binary->size); xe_dxil_shader_binary_delete(binary); if (!result) return 0;
  ps.program.float_constant_count = 8; ps.program.memexport_mask = pixel ? 31 : 0;
  ps.system_constants_bytes = 256; ps.flags_word = 0; ps.scale_x = ps.scale_y = 1;
  if (variant == 6 || variant == 7) { ps.scale_x = 2; ps.scale_y = 3; system[63] = 2|(3<<8); }
  if (variant == 9 || variant == 10) {
    system[62] = UINT32_MAX;
    if (variant == 9) {
      memset(&rop,0,sizeof(rop)); rop.depth.system_constants_bytes = 256;
      rop.depth.scale_x = rop.depth.scale_y = 1; rop.depth.counter_word = 62; ps.rop = &rop;
    } else { ps.count_coverage = 1; ps.counter_word = 62; ps.counter_stride_bytes = 16; }
  }
  ps.color_bias_word = 4; ps.blend_constant_word = 8;
  count = guest_words(words,variant,0);
  if (!pixel) { memset(words,0,sizeof(words)); words[0] = 1; words[1] = 0x2000; count = 3; }
  binary = xe_dxil_build_guest_pixel_owned(&ps,words,count,pixel ? 4 : 2); if (!binary) return 0;
  result = write_file(path,".ps",binary->data,binary->size); xe_dxil_shader_binary_delete(binary);
  return result && write_file(path,".system",system,sizeof(system));
}
int main(int argc, char **argv) {
  XeDxilShaderModule *shader = NULL; XeDxilShaderBinary binary; struct dxil_module *m;
  XeDxilAlu alu; XeDxilAluIo io; XeDxilValue output, cbv, ea[4], data[5][4], written, allowed, value;
  unsigned i, j, config, variant = 0, program_mode, stage_mode, block = 0; uint32_t constants[64], expected[32], dummy[4] = {0,0,0,0}; int result = 1;
  if (argc < 3 || argc > 5) return 2;
  program_mode = argc >= 4 && !strcmp(argv[3],"program");
  stage_mode = argc >= 4 ? !strcmp(argv[3],"vertex") ? 1 : !strcmp(argv[3],"pixel") ? 2 : 0 : 0;
  if (argc == 5) variant = (unsigned)strtoul(argv[4],NULL,0);
  if (variant > 10) return 2;
  config = (unsigned)strtoul(argv[2],NULL,0);
  if (variant == 1) config = (config&4095)|4096;
  if (variant == 2 || variant == 4) config = (config&4095)|(14u<<12);
  if (variant == 8) config = (config&4095)|(13u<<12);
  reference(config,constants,expected);
  constants[30] = bits(.5f); constants[31] = bits(1);
  if (variant == 6) constants[28] = bits(.5f);
  if (variant == 7) { constants[28] = bits(-.5f); for (i = 0; i < 32; ++i) expected[i] = 0xa5c39e71; }
  if (!write_file(argv[1],".expected",expected,sizeof(expected)) || !write_file(argv[1],".constants",constants,sizeof(constants)) || !write_file(argv[1],".input",dummy,sizeof(dummy))) return 1;
  if (stage_mode) return !stages(argv[1],stage_mode == 2,variant);
  if (argc >= 4 && !program_mode) return 0;
  memset(&binary,0,sizeof(binary)); memset(&io,0,sizeof(io)); io.current_block = &block;
  shader = xe_dxil_shader_create(DXIL_COMPUTE_SHADER); if (!shader) goto done;
  m = xe_dxil_shader_module(shader); xe_dxil_alu_init(&alu,m);
  if (!xe_dxil_shader_threads(shader,1,1,1) || xe_dxil_shader_resource(shader,XE_DXIL_RESOURCE_UAV,DXIL_RESOURCE_KIND_RAW_BUFFER,0,0,1,0) != 0 ||
      xe_dxil_shader_resource(shader,XE_DXIL_RESOURCE_CBV,DXIL_RESOURCE_KIND_CBUFFER,1,4,1,sizeof(constants)) != 0) goto done;
  output = xe_dxil_resource_handle(&alu,XE_DXIL_RESOURCE_UAV,0,0); cbv = xe_dxil_resource_handle(&alu,XE_DXIL_RESOURCE_CBV,0,4);
  for (i = 0; i < 32; ++i) if (!xe_dxil_raw_buffer_store(&alu,output,dxil_module_get_int32_const(m,(int32_t)(i*4)),dxil_module_get_int32_const(m,(int32_t)0xa5c39e71))) goto done;
  for (i = 0; i < 4; ++i) {
    value = xe_dxil_cbuffer_word(&alu,cbv,dxil_module_get_int32_const(m,(int32_t)i));
    ea[i] = value ? dxil_emit_cast(m,DXIL_CAST_BITCAST,dxil_module_get_float_type(m,32),value) : NULL;
  }
  for (i = 0; i < 5; ++i) for (j = 0; j < 4; ++j) {
    value = xe_dxil_cbuffer_word(&alu,cbv,dxil_module_get_int32_const(m,(int32_t)(4+i*4+j)));
    data[i][j] = value ? dxil_emit_cast(m,DXIL_CAST_BITCAST,dxil_module_get_float_type(m,32),value) : NULL;
  }
  written = xe_dxil_cbuffer_word(&alu,cbv,dxil_module_get_int32_const(m,24));
  value = xe_dxil_cbuffer_word(&alu,cbv,dxil_module_get_int32_const(m,25));
  allowed = value ? dxil_emit_cmp(m,DXIL_ICMP_NE,value,dxil_module_get_int32_const(m,0)) : NULL;
  if (program_mode) { if (!guest_program(m,cbv,output,allowed,variant)) goto done; }
  else if (!xe_dxil_memexport_store(&alu,&io,output,ea,data,31,written,allowed)) goto done;
  if (!alu.failed && dxil_emit_ret_void(m) && xe_dxil_shader_finish(shader,&binary) && write_file(argv[1],"",binary.data,binary.size)) result = 0;
done:
  xe_dxil_shader_binary_free(&binary); xe_dxil_shader_destroy(shader); return result;
}
