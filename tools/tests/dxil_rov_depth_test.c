/* Native ROV depth container fixture. MSVC C89; BSD license. */
#include "rov_depth.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv) {
  XeDxilRovDepthOptions o;
  XeDxilShaderBinary binaries[3][2], *binary;
  FILE *file;
  unsigned config;
  int result;
  if (argc < 5 || argc > 7) return 2;
  memset(&o, 0, sizeof(o)); memset(binaries, 0, sizeof(binaries));
  o.msaa = (unsigned)strtoul(argv[2], NULL, 10);
  config = (unsigned)strtoul(argv[3], NULL, 10);
  o.scale_x = config ? 2 : 1; o.scale_y = config ? 3 : 1;
  o.survey = (unsigned)strtoul(argv[4], NULL, 10); o.full_counters = argc >= 6 ? (unsigned)strtoul(argv[5], NULL, 10) : 1;
  o.guest_linkage = argc == 7 ? (unsigned)strtoul(argv[6],NULL,10) : 0;
  if (o.guest_linkage > 1) return 2;
  o.system_constants_bytes = 64;
  o.flags_word = 0; o.pitch_word = 1; o.depth_base_word = 2; o.counter_word = 3;
  o.front_scale_word = 4; o.back_scale_word = 5; o.front_offset_word = 6; o.back_offset_word = 7;
  o.front_masks_word = 8; o.back_masks_word = 9; o.front_ops_word = 10; o.back_ops_word = 11;
  o.depth_stencil_mask = 1u<<17; o.float24_mask = 1u<<10;
  o.depth_write_mask = 1u<<21; o.stencil_mask = 1u<<22; o.depth_function_shift = 18;
  if (o.msaa > 2 || o.survey > 1 || !xe_dxil_build_rov_depth_set(&o, binaries)) return 1;
  binary = &binaries[o.msaa][o.survey];
  file = fopen(argv[1], "wb"); if (!file) { xe_dxil_rov_depth_set_free(binaries); return 1; }
  result = fwrite(binary->data, 1, binary->size, file) != binary->size;
  if (fclose(file)) result = 1;
  xe_dxil_rov_depth_set_free(binaries); return result;
}