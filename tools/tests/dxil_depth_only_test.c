/* Native host depth helper fixtures. MSVC C89; BSD license. */
#include "depth_only.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv) {
  XeDxilDepthOnlyOptions options;
  XeDxilShaderBinary binaries[3][2], *binary;
  FILE *file;
  int result;
  unsigned mode, count;
  if (argc != 4) return 2;
  memset(&options, 0, sizeof(options)); memset(binaries, 0, sizeof(binaries));
  options.depth_mode = (unsigned)strtoul(argv[2], NULL, 10);
  options.count_coverage = (unsigned)strtoul(argv[3], NULL, 10);
  if (options.depth_mode > 2 || options.count_coverage > 1) return 2;
  /* A failure after generating the first shader must release the whole set. */
  if (xe_dxil_build_depth_only_set(&options, 1, 1, binaries)) return 1;
  for (mode = 0; mode < 3; ++mode) for (count = 0; count < 2; ++count)
    if (binaries[mode][count].data || binaries[mode][count].size) return 1;
  options.system_constants_bytes = 16; options.counter_stride_bytes = 16;
  if (!xe_dxil_build_depth_only_set(&options, 1, 1, binaries)) return 1;
  binary = &binaries[options.depth_mode][options.count_coverage];
  file = fopen(argv[1], "wb");
  if (!file) { xe_dxil_depth_only_set_free(binaries); return 1; }
  result = fwrite(binary->data, 1, binary->size, file) != binary->size;
  if (fclose(file)) result = 1;
  xe_dxil_depth_only_set_free(binaries); return result;
}
