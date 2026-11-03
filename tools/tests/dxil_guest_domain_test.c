/* Native domain adapter / fixed host tessellation fixture. MSVC C89. */
#include "guest_vertex_stage.h"
#include <dxil_module.h>
#include <dxil_function.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int write_file(const char *base, const char *suffix, const void *data, size_t size) {
  char path[2048]; FILE *file; int result;
  if (strlen(base)+strlen(suffix) >= sizeof(path)) return 0;
  strcpy(path,base); strcat(path,suffix); file = fopen(path,"wb"); if (!file) return 0;
  result = fwrite(data,1,size,file) == size; return fclose(file) == 0 && result;
}
static uint32_t bits(float v) { uint32_t b; memcpy(&b,&v,4); return b; }
static int probe(void *context, XeDxilGuestProgram *program, unsigned index, unsigned mask) {
  XeDxilAlu *alu = xe_dxil_guest_program_alu(program); XeDxilAluIo *io = xe_dxil_guest_program_io(program);
  const struct dxil_func *f; XeDxilValue args[2], values[4]; unsigned i, domain = *(unsigned *)context;
  (void)mask; if (index != 62) return 1;
  f = dxil_get_function(alu->module,"dx.op.domainLocation",DXIL_F32);
  for (i = 0; i < 4; ++i) {
    args[0] = dxil_module_get_int32_const(alu->module,105); args[1] = dxil_module_get_int8_const(alu->module,(int8_t)i);
    values[i] = i < (domain == 2 ? 3u : 2u) && f ? dxil_emit_call(alu->module,f,args,2) : dxil_module_get_float_const(alu->module,0);
  }
  return io->write(io->context,1,2,15,values,NULL);
}
int main(int argc, char **argv) {
  static const uint32_t base_words[] = {
    0x4001,0x2000,0,
    (50u<<26)|(15u<<16)|(1u<<15)|2,0,2u<<24,
    (50u<<26)|(15u<<16)|(1u<<15)|62,0,2u<<24,
    (50u<<26)|(15u<<16)|(1u<<15),0,0xc2000000,
    (50u<<26)|(15u<<16)|(1u<<15)|1,0,0xc2010100
  };
  XeDxilGuestVertexOptions o; XeDxilShaderBinary *binary; uint32_t words[24], system[192], constants[64], expected[32];
  XeDxilTextureLayout layout; XeDxilTextureStageOptions textures; XeDxilTextureInstruction instruction;
  unsigned config, mode, flags, i, slots = 4, slot, hosts[2]; int result;
  if (argc != 3) return 2;
  config = (unsigned)strtoul(argv[2],NULL,0); flags = config&~15u; config &= 15;
  if (flags > 240 || config > 11 || (config >= 8 && !(config&1))) return 2;
  mode = config/4; config %= 4;
  if (!xe_dxil_guest_tessellation_hosts(mode,3+config,&hosts[0],&hosts[1])) return 1;
  memset(&o,0,sizeof(o)); memset(system,0,sizeof(system)); memset(constants,0,sizeof(constants)); memset(expected,0,sizeof(expected));
  memset(words,0,sizeof(words)); memcpy(words,base_words,sizeof(base_words));
  o.program.register_count = 2; o.program.float_constant_count = 1; o.interpolator_mask = 7;
  o.domain = config < 2 ? 2 : 3; o.control_points = config&1 ? 1 : o.domain == 2 ? 3 : 4;
  o.program.context = &o.domain; o.program.export_written = probe;
  o.system_constants_bytes = sizeof(system); o.ndc_scale_word = 4; o.ndc_offset_word = 8;
  o.index_endian_word = 1; o.index_base_word = 2; o.w_not_reciprocal_mask = 32;
  if (flags&16) {
    memset(&layout,0,sizeof(layout)); memset(&textures,0,sizeof(textures)); memset(&instruction,0,sizeof(instruction));
    instruction.opcode = 1; instruction.dimension = 1;
    if (!xe_dxil_texture_layout_add(&layout,&instruction,0)) return 1;
    textures.layout = &layout; textures.system_constants_bytes = sizeof(system); textures.signs_word = 140; textures.integer_scale_word = 148; o.textures = &textures;
    o.program.register_count = 16;
    slot = 3+slots*3; words[0] |= 1u<<(16+slots*2); ++slots;
    words[slot] = 1|(15u<<12)|(36u<<26); words[slot+1] = 1672; words[slot+2] = 1u<<14;
  }
  if (flags&32) {
    o.program.memexport_mask = 1; o.program.float_constant_count = 2;
    slot = 3+(slots++)*3; words[slot] = (50u<<26)|(15u<<16)|(1u<<15)|32; words[slot+2] = (2u<<24)|(1u<<16)|(1u<<8);
    slot = 3+(slots++)*3; words[slot] = (50u<<26)|(15u<<16)|(1u<<15)|33; words[slot+2] = 0xc2000000;
  }
  if (flags&64) { o.clip_planes = 6; o.clip_planes_word = 16; }
  if (flags&128) { o.clip_planes = 0; o.cull_planes = 6; o.clip_planes_word = 16; o.point_size = o.vertex_kill = o.vertex_kill_and = 1; }
  words[0] = (words[0]&~0x7000u)|(slots<<12);
  system[0] = 32; system[4] = system[5] = system[6] = bits(1);
  system[128] = system[129] = bits(mode ? 2.0f : 1.0f); system[133] = 17; system[135] = 0xffffff;
  constants[2] = bits(.5f); constants[3] = bits(1);
  for (i = 0; i < 32; ++i) expected[i] = 0xa5c39e71;
  binary = xe_dxil_build_guest_vertex_owned(&o,words,3+slots*3,2); if (!binary) return 1;
  result = write_file(argv[1],".ds",binary->data,binary->size) && write_file(argv[1],".system",system,sizeof(system)) &&
    write_file(argv[1],".constants",constants,sizeof(constants)) && write_file(argv[1],".expected",expected,sizeof(expected)) &&
    write_file(argv[1],".hosts",hosts,sizeof(hosts));
  xe_dxil_shader_binary_delete(binary); return !result;
}
