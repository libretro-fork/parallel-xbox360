/* Native vertex stage container fixture. MSVC C89; BSD license. */
#include "guest_vertex_stage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int observed_export(void *context, XeDxilGuestProgram *program, unsigned index, unsigned mask) {
  unsigned *count = (unsigned *)context;
  (void)program; (void)index; (void)mask;
  ++*count; return 1;
}
int main(int argc, char **argv) {
  static const uint32_t code[] = {
    0x14001,0x2000,0,
    1u<<12,(38u<<16)|1672,4,
    (50u<<26)|(15u<<16)|(1u<<15)|62,0,0xc2010100,
    (50u<<26)|(15u<<16)|(1u<<15),0,0xc2000000,
    (50u<<26)|(15u<<16)|(1u<<15)|63,0,0x02000000,
    (50u<<26)|(15u<<16)|(1u<<15)|3,0,0x02010100,
    (50u<<26)|(15u<<16)|(1u<<15)|15,0,0xc2010100
  };
  XeDxilGuestVertexOptions options;
  XeDxilShaderBinary *binary;
  uint32_t words[21];
  unsigned config, observed = 0;
  FILE *file;
  int result;
  if (argc != 3) return 2;
  config = (unsigned)strtoul(argv[2],NULL,10); if (config > 8) return 2;
  memset(&options,0,sizeof(options)); memcpy(words,code,sizeof(code));
  options.program.context = &observed; options.program.export_written = observed_export;
  options.program.register_count = 2; options.program.float_constant_count = 2;
  options.system_constants_bytes = 256;
  options.flags_word = 0; options.index_endian_word = 1; options.index_base_word = 2;
  options.ndc_scale_word = 4; options.ndc_offset_word = 8; options.clip_planes_word = 16;
  options.interpolator_mask = config == 5 ? 0x8009 : 1; options.point_size = config >= 1 && config <= 4;
  options.clip_planes = config == 1 ? 6 : config == 3 ? 3 : 0;
  options.cull_planes = config == 2 ? 6 : 0;
  options.vertex_kill = config >= 1 && config <= 3; options.vertex_kill_and = config == 1 || config == 2;
  options.w_not_reciprocal_mask = 1u<<5; options.xy_divided_mask = 1u<<3; options.z_divided_mask = 1u<<4;
  if (config == 4) words[0] = 0x13001;
  if (config == 5) words[0] = 0x16001;
  if (config >= 6) {
    words[0] |= 1u<<20;
    words[9] = (config == 8 ? 18u : 19u)|(36u<<26)|(config == 7 ? 1u<<25 : 0); words[10] = 1672; words[11] = 1u<<14;
    words[12] = (50u<<26)|(15u<<16)|(1u<<15); words[13] = 0; words[14] = 0xc2000000;
  }
  binary = xe_dxil_build_guest_vertex_owned(&options,words,sizeof(words)/4,2);
  if (config == 8) { if (!binary) return 0; xe_dxil_shader_binary_delete(binary); return 1; }
  if (!binary) return 1;
  if (observed != (config == 4 || config >= 6 ? 2u : config == 5 ? 5u : 3u)) { xe_dxil_shader_binary_delete(binary); return 1; }
  file = fopen(argv[1],"wb"); if (!file) { xe_dxil_shader_binary_delete(binary); return 1; }
  result = fwrite(binary->data,1,binary->size,file) != binary->size;
  if (fclose(file)) result = 1;
  xe_dxil_shader_binary_delete(binary); return result;
}
