/* Native EDRAM dump generator and execution data. MSVC C89; BSD license. */
#include "edram_dump.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int write_file(const char *path, const void *data, size_t size) {
  FILE *file = fopen(path, "wb");
  int result;
  if (!file) return 0;
  result = fwrite(data, 1, size, file) == size;
  if (fclose(file)) result = 0;
  return result;
}
int main(int argc, char **argv) {
  static const unsigned component[4] = {0,64,128,191};
  XeDxilEdramDumpOptions options;
  XeDxilShaderBinary binary;
  float texture[80];
  uint32_t expected[128], constants[4] = {1025,0,0,0};
  unsigned config, x, y, i;
  char *path;
  size_t length;
  int result = 1;
  if (argc != 6) return 2;
  memset(&options, 0, sizeof(options)); memset(&binary, 0, sizeof(binary));
  options.format = (unsigned)strtoul(argv[2], NULL, 10);
  options.depth = (unsigned)strtoul(argv[3], NULL, 10);
  options.msaa = (unsigned)strtoul(argv[4], NULL, 10);
  config = (unsigned)strtoul(argv[5], NULL, 10);
  if (config > 3) return 2;
  options.scale_x = config ? 2 : 1; options.scale_y = config ? 3 : 1;
  options.native_source = config >= 2; options.native_layout = config == 3;
  options.source_uint = (options.format >= 4 && options.format <= 7) || options.format >= 14;
  options.msaa_2x_supported = config != 1; options.round_depth = config & 1;
  options.depth_converted_in_pixel_shader = config == 2;
  if (!xe_dxil_build_edram_dump(&options, &binary)) return 1;
  if (!write_file(argv[1], binary.data, binary.size)) goto done;
  /* The execution fixture uses RGBA32F input and an 8x16 dispatch region
   * inside a single 80x16 EDRAM tile. Unwritten columns aren't compared. */
  if (options.format || options.depth || options.msaa || config) { result = 0; goto done; }
  memset(texture, 0, sizeof(texture));
  for (y = 0; y < 4; ++y) for (x = 0; x < 4; ++x) {
    i = (y * 4 + x) * 4; texture[i] = (float)x * .25f; texture[i+1] = (float)y * .25f;
    texture[i+2] = .5f; texture[i+3] = 1.0f;
  }
  for (y = 0; y < 16; ++y) for (x = 0; x < 8; ++x)
    expected[y * 8 + x] = x < 4 && y < 4 ? UINT32_C(0xff800000) | (component[y] << 8) | component[x] : 0;
  length = strlen(argv[1]); path = (char *)malloc(length + 11);
  if (!path) goto done;
  memcpy(path, argv[1], length); memcpy(path + length, ".texture", 9);
  if (!write_file(path, texture, sizeof(texture))) { free(path); goto done; }
  memcpy(path + length, ".expected", 10);
  if (!write_file(path, expected, sizeof(expected))) { free(path); goto done; }
  memcpy(path + length, ".constants", 11);
  result = !write_file(path, constants, sizeof(constants)); free(path);
done:
  xe_dxil_shader_binary_free(&binary);
  return result;
}
