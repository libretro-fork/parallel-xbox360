/* Native texture sampling fixture. MSVC C89; BSD license. */
#include "guest_texture.h"
#include "guest_resources.h"
#include "dxil_test_storage.h"
#include <dxil_module.h>

typedef struct SampleCase {
  unsigned mode, linear;
  float u, v, lod, gradient;
  int x_offset, y_offset;
  float expected[4];
} SampleCase;
static const SampleCase cases[] = {
  {0,0,0.125f,0.125f,0,0,0,0,{1,1,1,1}},
  {0,0,0.875f,0.875f,0,0,0,0,{4,4,16,1}},
  {0,0,0.125f,0.125f,0,0,1,1,{2,2,6,1}},
  {0,0,0.375f,0.375f,0,0,-1,0,{1,2,5,1}},
  {0,0,0.25f,0.25f,1,0,0,0,{20,21,22,23}},
  {0,0,0.75f,0.75f,1,0,0,0,{32,33,34,35}},
  {1,0,0.25f,0.25f,0,0.5f,0,0,{20,21,22,23}},
  {1,0,0.375f,0.375f,0,0.25f,0,0,{2,2,6,1}},
  {0,1,0.25f,0.25f,0,0,0,0,{1.5f,1.5f,3.5f,1}},
  {0,1,0.125f,0.125f,0.5f,0,0,0,{10.5f,11,11.5f,12}},
  {0,0,-0.5f,2,0,0,0,0,{1,4,13,1}}
};

static int decode_test(void) {
  static const unsigned opcodes[] = {1,16,17,18,19,24,25,26};
  XeDxilTextureInstruction instruction;
  uint32_t words[3];
  unsigned i;
  words[0] = (63u << 5) | (1u << 11) | (62u << 12) | (1u << 18) | (1u << 19) |
    (31u << 20) | (1u << 25) | (57u << 26);
  words[1] = 1672 | (3u << 12) | (2u << 14) | (1u << 16) | (7u << 18) |
    (5u << 21) | (2u << 24) | (1u << 26) | (1u << 28) | (1u << 29) | (1u << 31);
  words[2] = 3 | (64u << 2) | (3u << 14) | (16u << 16) | (15u << 21) | (31u << 26) | (1u << 31);
  for (i = 0; i < ARRAY_SIZE(opcodes); ++i) {
    words[0] = (words[0] & ~31u) | opcodes[i];
    if (!xe_dxil_decode_texture(words, 3, &instruction) || instruction.opcode != opcodes[i] ||
        instruction.source != 63 || instruction.destination != 62 || !instruction.source_relative ||
        !instruction.destination_relative || !instruction.valid_only || instruction.constant_index != 31 ||
        !instruction.unnormalized || instruction.source_swizzle != 57 || instruction.destination_swizzle != 1672 ||
        instruction.mag_filter != 3 || instruction.min_filter != 2 || instruction.mip_filter != 1 ||
        instruction.aniso_filter != 7 || instruction.arbitrary_filter != 5 || instruction.volume_mag_filter != 2 ||
        instruction.volume_min_filter != 1 || !instruction.computed_lod || !instruction.register_lod ||
        !instruction.predicated || !instruction.register_gradients || !instruction.sample_location ||
        instruction.lod_bias_sixteenths != -64 || instruction.dimension != 3 ||
        instruction.offset_halves[0] != -16 || instruction.offset_halves[1] != 15 ||
        instruction.offset_halves[2] != -1 || !instruction.predicate_condition) return 0;
  }
  words[2] = 63u << 2;
  if (!xe_dxil_decode_texture(words, 3, &instruction) || instruction.lod_bias_sixteenths != 63 ||
      xe_dxil_decode_texture(words, 2, &instruction)) return 0;
  words[0] &= ~31u;
  return !xe_dxil_decode_texture(words, 3, &instruction);
}

int xe_dxil_texture_test(struct dxil_module *m, const char *path) {
  XeDxilAlu alu;
  XeDxilTextureSample sample;
  XeDxilValue handle, texture, samplers[2], values[4];
  float texels[80];
  uint32_t expected[8];
  unsigned i, j, x, y;
  size_t length = strlen(path);
  char *name;
  FILE *file, *golden;
  int result = 0;
  if (!decode_test() || length > (size_t)-1 - 10) return 0;
  for (y = 0; y < 4; ++y) for (x = 0; x < 4; ++x) {
    i = (y * 4 + x) * 4;
    texels[i] = (float)(x + 1); texels[i + 1] = (float)(y + 1);
    texels[i + 2] = (float)(y * 4 + x + 1); texels[i + 3] = 1.0f;
  }
  for (i = 0; i < 16; ++i) texels[64 + i] = (float)(20 + i);
  name = (char *)malloc(length + 10);
  if (!name) return 0;
  memcpy(name, path, length); memcpy(name + length, ".texture", 9);
  file = fopen(name, "wb");
  if (!file) { free(name); return 0; }
  result = fwrite(texels, 1, sizeof(texels), file) == sizeof(texels);
  if (fclose(file)) result = 0;
  if (!result) { free(name); return 0; }
  memcpy(name + length, ".expected", 10); golden = fopen(name, "wb"); free(name);
  if (!golden) return 0;
  result = 0;
  xe_dxil_alu_init(&alu, m);
  handle = xe_dxil_resource_handle(&alu, XE_DXIL_RESOURCE_UAV, 0, 0);
  texture = xe_dxil_resource_handle(&alu, XE_DXIL_RESOURCE_SRV, 0, 0);
  samplers[0] = xe_dxil_resource_handle(&alu, XE_DXIL_RESOURCE_SAMPLER, 0, 0);
  samplers[1] = xe_dxil_resource_handle(&alu, XE_DXIL_RESOURCE_SAMPLER, 1, 1);
  if (!handle || !texture || !samplers[0] || !samplers[1]) goto done;
  for (i = 0; i < ARRAY_SIZE(cases); ++i) {
    memset(&sample, 0, sizeof(sample)); sample.texture = texture; sample.sampler = samplers[cases[i].linear];
    sample.mode = cases[i].mode; sample.dimension = XE_DXIL_SAMPLE_2D;
    sample.coordinates[0] = dxil_module_get_float_const(m, cases[i].u);
    sample.coordinates[1] = dxil_module_get_float_const(m, cases[i].v);
    sample.lod_or_bias = dxil_module_get_float_const(m, cases[i].lod);
    sample.offsets[0] = cases[i].x_offset; sample.offsets[1] = cases[i].y_offset;
    for (j = 0; j < 2; ++j) {
      sample.ddx[j] = dxil_module_get_float_const(m, j == 0 ? cases[i].gradient : 0.0f);
      sample.ddy[j] = dxil_module_get_float_const(m, j == 1 ? cases[i].gradient : 0.0f);
    }
    if (!xe_dxil_texture_sample(&alu, &sample, values)) goto done;
    for (j = 0; j < 4; ++j) expected[j] = xe_dxil_test_float_bits(cases[i].expected[j]);
    expected[4] = 0; expected[5] = 0; expected[6] = 0; expected[7] = UINT32_C(0xa0000000) | i;
    if (!xe_dxil_test_record(m, handle, i, &alu, values, expected, golden)) goto done;
  }
  /* Implicit sampling is illegal in this SM6.0 compute fixture. */
  sample.mode = XE_DXIL_SAMPLE_IMPLICIT;
  if (xe_dxil_texture_sample(&alu, &sample, values) || !alu.failed) goto done;
  xe_dxil_alu_init(&alu, m); sample.mode = XE_DXIL_SAMPLE_LEVEL; sample.offsets[0] = 8;
  if (xe_dxil_texture_sample(&alu, &sample, values) || !alu.failed) goto done;
  xe_dxil_alu_init(&alu, m); sample.offsets[0] = 0;
  sample.coordinates[0] = dxil_module_get_int32_const(m, 0);
  if (xe_dxil_texture_sample(&alu, &sample, values) || !alu.failed) goto done;
  xe_dxil_alu_init(&alu, m); sample.coordinates[0] = dxil_module_get_float_const(m, 0.5f);
  sample.mode = XE_DXIL_SAMPLE_GRAD; sample.ddx[0] = NULL;
  if (xe_dxil_texture_sample(&alu, &sample, values) || !alu.failed) goto done;
  result = dxil_emit_ret_void(m);
done:
  if (fclose(golden)) result = 0;
  return result;
}

/* Validator fixture for derivative-based pixel-stage operations. */
int xe_dxil_texture_pixel_test(struct dxil_module *m, XeDxilValue values[4]) {
  XeDxilAlu alu;
  XeDxilTextureSample sample;
  memset(&sample, 0, sizeof(sample)); xe_dxil_alu_init(&alu, m);
  sample.texture = xe_dxil_resource_handle(&alu, XE_DXIL_RESOURCE_SRV, 0, 0);
  sample.sampler = xe_dxil_resource_handle(&alu, XE_DXIL_RESOURCE_SAMPLER, 0, 0);
  sample.dimension = XE_DXIL_SAMPLE_2D; sample.mode = XE_DXIL_SAMPLE_IMPLICIT;
  sample.coordinates[0] = dxil_module_get_float_const(m, 0.5f);
  sample.coordinates[1] = sample.coordinates[0];
  if (!xe_dxil_texture_sample(&alu, &sample, values)) return 0;
  sample.mode = XE_DXIL_SAMPLE_BIAS;
  sample.lod_or_bias = dxil_module_get_float_const(m, 0.5f);
  return xe_dxil_texture_sample(&alu, &sample, values);
}
