/* Ownership-transfer container fixtures. MSVC C89; BSD license. */
#include "edram_transfer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv) {
  XeDxilEdramTransferOptions options;
  XeDxilShaderBinary binary;
  unsigned mode, variant;
  FILE *file;
  int result;
  if (argc != 4 && argc != 6 && argc != 7) return 2;
  mode = (unsigned)strtoul(argv[2], NULL, 10);
  variant = (unsigned)strtoul(argv[3], NULL, 10);
  memset(&options, 0, sizeof(options));
  memset(&binary, 0, sizeof(binary));
  options.mode = mode;
  options.source_msaa = variant % 3;
  options.destination_msaa = (variant / 3) % 3;
  options.host_depth_msaa = mode >= 8 ? 0 : variant % 3;
  options.scale_x = 2; options.scale_y = 3;
  options.source_native = (variant / 9) & 1;
  options.destination_native = (variant / 18) & 1;
  options.msaa_2x_supported = variant & 1;
  options.stencil_reference = variant & 1;
  options.round_depth = variant & 1;
  options.source_space = 2; options.host_depth_space = 3;
  if (argc >= 6) {
    options.source_format = (unsigned)strtoul(argv[4], NULL, 10);
    options.destination_format = (unsigned)strtoul(argv[5], NULL, 10);
    options.source_uint = (options.source_format >= 4 && options.source_format <= 7) || options.source_format >= 14;
    options.destination_uint = (options.destination_format >= 4 && options.destination_format <= 7) || options.destination_format >= 14;
  }
  if (argc == 7) {
    unsigned flags = (unsigned)strtoul(argv[6], NULL, 10);
    options.no_discard_stencil = flags & 1;
    options.value_convert = (flags >> 1) & 1;
    options.depth_converted_in_pixel_shader = (flags >> 2) & 1;
  }
  if (!xe_dxil_build_edram_transfer(&options, &binary)) return 1;
  file = fopen(argv[1], "wb");
  if (!file) { xe_dxil_shader_binary_free(&binary); return 1; }
  result = fwrite(binary.data, 1, binary.size, file) != binary.size;
  if (fclose(file)) result = 1;
  xe_dxil_shader_binary_free(&binary);
  return result;
}
