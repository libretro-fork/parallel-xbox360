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
  uint32_t expected[8192], constants[12] = {1025,0,1,32,0,1025,0,1,0,0,0,0};
  static const unsigned row_offsets[8] = {0,16,256,272,512,528,768,784};
  static const unsigned column_offsets[8] = {0,4,8,12,32,36,40,44};
  static const unsigned scaled_rows[8] = {0,64,128,192,256,320,1536,1600};
  static const unsigned rows64[8] = {0,16,512,528,1024,1040,1536,1552};
  static const unsigned columns64[8] = {0,8,32,40,256,264,288,296};
  static const unsigned scaled_rows64[8] = {0,32,64,96,128,160,3072,3104};
  static const unsigned scaled_columns64[16] = {0,8,16,24,192,200,208,216,1536,1544,1552,1560,1728,1736,1744,1752};
  static const unsigned permutations[4][4] = {{0,1,2,3},{1,0,3,2},{3,2,1,0},{2,3,0,1}};
  unsigned config, x, y, i, test_case, sx, sy, byte, endian, red, blue, array_base, wide, word;
  uint32_t color, swapped, pair[2], bits;
  char *path;
  size_t length;
  int result = 1;
  if (argc < 6 || argc > 8) return 2;
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
  options.direct_resolve = argc >= 7 ? (unsigned)strtoul(argv[6], NULL, 10) : 0;
  if (options.direct_resolve > 1) return 2;
  test_case = argc == 8 ? (unsigned)strtoul(argv[7], NULL, 10) : 0;
  if (test_case > 15) return 2;
  wide = options.format == 15 && test_case >= 8;
  if (wide) options.source_uint = 0;
  test_case &= 7;
  options.direct_resolve_3d = options.direct_resolve && (test_case == 5 || test_case == 6);
  if (!xe_dxil_build_edram_dump(&options, &binary)) return 1;
  if (!write_file(argv[1], binary.data, binary.size)) goto done;
  /* The execution fixture uses RGBA32F input and an 8x16 dispatch region
   * inside a single 80x16 EDRAM tile. Unwritten columns aren't compared. */
  if ((options.format && !wide) || options.depth || options.msaa || (!options.direct_resolve && config)) { result = 0; goto done; }
  endian = test_case <= 3 ? test_case : test_case == 6 ? 2 : 0;
  constants[4] = endian;
  if (test_case == 4) constants[4] |= 1u << 24;
  if (test_case == 5 || test_case == 6) constants[4] |= 8 | (1u << 4);
  if (test_case == 7) constants[2] |= 1u << 29;
  if (wide) {
    constants[2] |= (test_case == 4 ? 5u : 15u) << 24;
    if (test_case == 6) { endian = 4; constants[4] = (constants[4] & ~7u) | 4; }
  }
  memset(texture, 0, sizeof(texture));
  for (y = 0; y < 4; ++y) for (x = 0; x < 4; ++x) {
    i = (y * 4 + x) * 4; texture[i] = (float)x * .25f; texture[i+1] = (float)y * .25f;
    texture[i+2] = .5f; texture[i+3] = 1.0f;
    if (wide) {
      texture[i] = .25f + (float)x * .125f; texture[i+1] = .125f + (float)y * .125f;
      memcpy(&bits, &texture[i], 4); bits |= 0x123; memcpy(&texture[i], &bits, 4);
      memcpy(&bits, &texture[i+1], 4); bits |= 0x456; memcpy(&texture[i+1], &bits, 4);
    }
  }
  memset(expected, 0, sizeof(expected));
  for (y = 0; y < (options.direct_resolve ? 8u : 16u); ++y)
    for (x = 0; x < (options.direct_resolve && config && config != 3 ? 16u : 8u); ++x) {
    sx = x; sy = y;
    if (options.direct_resolve && config && config != 3) {
      if (test_case == 7) { if (!sx) sx = 1; if (!sy) sy = 1; }
      if (options.native_source) { sx /= 2; sy /= 3; }
    }
    red = sx < 4 ? component[sx] : 0; blue = 128;
    if (options.direct_resolve && test_case == 4) { byte = red; red = blue; blue = byte; }
    color = sx < 4 && sy < 4 ? UINT32_C(0xff000000) | (blue << 16) | (component[sy] << 8) | red : 0;
    if (options.direct_resolve) {
      array_base = test_case == 5 || test_case == 6 ? wide ? 4096 : 1024 : 0;
      if (config && config != 3) i = (array_base * 6 + (wide ? scaled_rows64[y] + scaled_columns64[x] : scaled_rows[y] + x * 4)) / 4;
      else i = (array_base + (wide ? rows64[y] + columns64[x] : row_offsets[y] + column_offsets[x])) / 4;
    } else i = y * 8 + x;
    pair[0] = color; pair[1] = 0;
    if (wide) {
      pair[0] = 0;
      if (sx < 4 && sy < 4) { memcpy(&pair[0], &texture[(sy * 4 + sx) * 4], 4); memcpy(&pair[1], &texture[(sy * 4 + sx) * 4 + 1], 4); }
      if (test_case == 4) { bits = pair[0]; pair[0] = (pair[0] & 0xffff0000) | (pair[1] & 65535); pair[1] = (pair[1] & 0xffff0000) | (bits & 65535); }
      if (endian == 4) { bits = pair[0]; pair[0] = pair[1]; pair[1] = bits; }
    }
    for (word = 0; word < (wide ? 2u : 1u); ++word) {
      swapped = 0;
      for (byte = 0; byte < 4; ++byte) swapped |= ((pair[word] >> (permutations[endian == 4 ? 2 : endian][byte] * 8)) & 255) << (byte * 8);
      expected[i + word] = swapped;
    }
  }
  length = strlen(argv[1]); path = (char *)malloc(length + 11);
  if (!path) goto done;
  memcpy(path, argv[1], length); memcpy(path + length, ".texture", 9);
  if (!write_file(path, texture, sizeof(texture))) { free(path); goto done; }
  memcpy(path + length, ".expected", 10);
  if (!write_file(path, expected, options.direct_resolve ? sizeof(expected) : 128 * sizeof(expected[0]))) { free(path); goto done; }
  memcpy(path + length, ".constants", 11);
  result = !write_file(path, constants, sizeof(constants)); free(path);
done:
  xe_dxil_shader_binary_free(&binary);
  return result;
}
