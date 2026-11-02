/* Native texture sampling fixture. MSVC C89; BSD license. */
#include "guest_texture.h"
#include "guest_resources.h"
#include "guest_flow.h"
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

typedef struct TextureFixture {
  InstructionStorage storage;
  XeDxilAlu *alu;
  XeDxilTextureFetch fetch;
  XeDxilValue texture, sampler, samples;
} TextureFixture;

static int guest_sample(void *context, const XeDxilTextureInstruction *instruction,
                          const XeDxilTextureOperands *operands, XeDxilValue result[4]) {
  TextureFixture *fixture = (TextureFixture *)context;
  struct dxil_module *m = fixture->storage.module;
  XeDxilTextureSample sample;
  XeDxilValue count;
  unsigned i;
  if (instruction->opcode != 1 || operands->coordinate_count != 2) return 0;
  count = dxil_emit_load(m, fixture->samples, 4, 0);
  count = count ? dxil_emit_binop(m, DXIL_BINOP_ADD, count, dxil_module_get_int32_const(m, 1), 0) : NULL;
  if (!count || !dxil_emit_store(m, count, fixture->samples, 4, 0)) return 0;
  memset(&sample, 0, sizeof(sample));
  sample.texture = fixture->texture; sample.sampler = fixture->sampler;
  sample.dimension = XE_DXIL_SAMPLE_2D;
  sample.mode = instruction->register_gradients ? XE_DXIL_SAMPLE_GRAD : XE_DXIL_SAMPLE_LEVEL;
  sample.lod_or_bias = instruction->register_lod ? operands->lod : dxil_module_get_float_const(m, 0.0f);
  for (i = 0; i < 2; ++i) {
    sample.coordinates[i] = operands->coordinates[i];
    sample.ddx[i] = operands->ddx[i]; sample.ddy[i] = operands->ddy[i];
  }
  return xe_dxil_texture_sample(fixture->alu, &sample, result);
}

static int guest_fetch(void *context, const uint32_t *words, unsigned serialized,
                         XeDxilAlu *alu, XeDxilAluIo *io) {
  TextureFixture *fixture = (TextureFixture *)context;
  XeDxilTextureInstruction instruction;
  (void)serialized;
  return xe_dxil_decode_texture(words, 3, &instruction) &&
    xe_dxil_emit_texture_fetch(alu, &instruction, io, &fixture->fetch);
}

static int guest_instruction(TextureFixture *fixture, XeDxilAluIo *io, unsigned opcode,
                               unsigned source, unsigned source_swizzle, unsigned destination,
                               unsigned destination_swizzle, unsigned flags, unsigned predicate) {
  uint32_t words[3];
  words[0] = opcode | (source << 5) | (destination << 12) | (source_swizzle << 26);
  words[1] = destination_swizzle | (flags & UINT32_C(0xe0000000));
  words[2] = (1u << 14) | (flags & 1) | (predicate << 31);
  if (flags & 2) words[0] |= 1u << 11;
  if (flags & 4) words[0] |= 1u << 18;
  return guest_fetch(fixture, words, 0, fixture->alu, io);
}

static int guest_texture_test(struct dxil_module *m, XeDxilAlu *alu, XeDxilValue handle,
                                TextureFixture *fixture, XeDxilAluIo *io, FILE *golden) {
  static const float expected_values[9][4] = {
    {2,2,6,1}, {20,21,22,23}, {20,21,22,23}, {2,2,6,1},
    {20,21,22,23}, {42,42,42,42}, {1,1,0,42}, {4,4,16,1}, {42,1,0,42}
  };
  static const unsigned sources[9] = {1,1,1,1,1,1,1,1,1};
  XeDxilGuestFlow flow;
  XeDxilValue values[4], pointer;
  uint32_t expected[8];
  unsigned i, j;
  /* A real EXEC_END clause invokes the texture hook three times: set LOD,
   * sample, then set horizontal gradients. No alternate shader format. */
  static const uint32_t program[] = {
    0x00153001,0x2000,0,
    24 | (1u << 5) | (2u << 26),0,0,
    1 | (1u << 5) | (2u << 12) | (4u << 26),1672 | (1u << 29),1u << 14,
    25 | (1u << 5) | (63u << 26),0,0
  };
  for (i = 0; i < 9; ++i) {
    io->loop_address = dxil_module_get_int32_const(m, 2);
    for (j = 0; j < 4; ++j) values[j] = dxil_module_get_float_const(m, 42.0f);
    if (!xe_dxil_test_write(&fixture->storage, 0, 2, 15, values, NULL) ||
        !xe_dxil_test_write(&fixture->storage, 0, 4, 15, values, NULL)) return 0;
    if (i == 1) {
      memset(&flow, 0, sizeof(flow)); flow.module = m; flow.alu = alu; flow.io = io;
      flow.words = program; flow.word_count = ARRAY_SIZE(program); flow.control_flow_count = 1;
      flow.context = fixture; flow.fetch = guest_fetch;
      if (!xe_dxil_emit_guest_flow(&flow)) return 0;
    } else {
      if (i == 2 && !guest_instruction(fixture, io, 24, 0, 0, 0, 4095, UINT32_C(0x80000000), 1)) return 0;
      if (i == 3 && !guest_instruction(fixture, io, 24, 0, 0, 0, 4095, UINT32_C(0x80000000), 0)) return 0;
      if (i == 4 && !guest_instruction(fixture, io, 26, 1, 63, 0, 4095, 0, 0)) return 0;
      if (!guest_instruction(fixture, io, 1, sources[i], 4, 2,
          i == 6 ? 3883 : i == 8 ? 3887 : 1672,
          i == 4 ? 1 : i == 5 ? UINT32_C(0x80000000) : i >= 6 ? 6 : UINT32_C(0x20000000),
          i == 5)) return 0;
    }
    for (j = 0; j < 4; ++j) {
      values[j] = xe_dxil_test_read(&fixture->storage, 0, i >= 6 ? 4 : 2, j, NULL);
      expected[j] = xe_dxil_test_float_bits(expected_values[i][j]);
    }
    expected[4] = 0; expected[5] = 0; expected[6] = 0; expected[7] = UINT32_C(0xa1000000) | i;
    if (!xe_dxil_test_record(m, handle, ARRAY_SIZE(cases) + i, alu, values, expected, golden)) return 0;
  }
  pointer = dxil_emit_load(m, fixture->samples, 4, 0);
  values[0] = pointer ? dxil_emit_cast(m, DXIL_CAST_UITOFP, dxil_module_get_float_type(m, 32), pointer) : NULL;
  for (j = 0; j < 4; ++j) { values[j] = values[0]; expected[j] = xe_dxil_test_float_bits(7.0f); }
  expected[7] = UINT32_C(0xa2000000);
  return xe_dxil_test_record(m, handle, ARRAY_SIZE(cases) + 9, alu, values, expected, golden);
}

int xe_dxil_texture_test(struct dxil_module *m, const char *path) {
  XeDxilAlu alu;
  XeDxilAluIo io;
  TextureFixture fixture;
  XeDxilTextureSample sample;
  XeDxilValue handle, texture, samplers[2], values[4];
  float texels[80];
  uint32_t expected[8];
  unsigned i, j, x, y, block = 0;
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
  memset(&fixture, 0, sizeof(fixture)); memset(&io, 0, sizeof(io));
  fixture.alu = &alu; fixture.texture = texture; fixture.sampler = samplers[0];
  fixture.storage.module = m; fixture.storage.current_block = &block;
  fixture.storage.registers = dxil_emit_alloca(m,
    dxil_module_get_array_type(m, dxil_module_get_float_type(m, 32), 256),
    dxil_module_get_int32_const(m, 1), 4);
  fixture.samples = dxil_emit_alloca(m, dxil_module_get_int_type(m, 32), dxil_module_get_int32_const(m, 1), 4);
  if (!fixture.storage.registers || !fixture.samples ||
      !dxil_emit_store(m, dxil_module_get_int32_const(m, 0), fixture.samples, 4, 0)) goto done;
  io.context = &fixture.storage; io.read = xe_dxil_test_read; io.write = xe_dxil_test_write;
  io.current_block = &block; io.loop_address = dxil_module_get_int32_const(m, 2);
  fixture.fetch.context = &fixture; fixture.fetch.fetch = guest_sample;
  if (!xe_dxil_texture_fetch_init(&alu, &fixture.fetch)) goto done;
  for (i = 0; i < 4; ++i) {
    for (j = 0; j < 4; ++j) values[j] = dxil_module_get_float_const(m,
      i == 1 ? j < 2 ? 0.25f : j == 2 ? 1.0f : 0.5f : i == 3 ? 0.75f : 0.0f);
    if (!xe_dxil_test_write(&fixture.storage, 0, i, 15, values, NULL)) goto done;
  }
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
  if (!guest_texture_test(m, &alu, handle, &fixture, &io, golden)) goto done;
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
  XeDxilAluIo io;
  InstructionStorage storage;
  XeDxilTextureFetch fetch;
  XeDxilTextureInstruction instruction;
  XeDxilTextureSample sample;
  unsigned block = 0, i;
  memset(&sample, 0, sizeof(sample)); xe_dxil_alu_init(&alu, m);
  sample.texture = xe_dxil_resource_handle(&alu, XE_DXIL_RESOURCE_SRV, 0, 0);
  sample.sampler = xe_dxil_resource_handle(&alu, XE_DXIL_RESOURCE_SAMPLER, 0, 0);
  sample.dimension = XE_DXIL_SAMPLE_2D; sample.mode = XE_DXIL_SAMPLE_IMPLICIT;
  sample.coordinates[0] = dxil_module_get_float_const(m, 0.5f);
  sample.coordinates[1] = sample.coordinates[0];
  if (!xe_dxil_texture_sample(&alu, &sample, values)) return 0;
  sample.mode = XE_DXIL_SAMPLE_BIAS;
  sample.lod_or_bias = dxil_module_get_float_const(m, 0.5f);
  if (!xe_dxil_texture_sample(&alu, &sample, values)) return 0;
  memset(&io, 0, sizeof(io)); memset(&storage, 0, sizeof(storage));
  memset(&fetch, 0, sizeof(fetch)); memset(&instruction, 0, sizeof(instruction));
  storage.module = m; storage.current_block = &block;
  storage.registers = dxil_emit_alloca(m,
    dxil_module_get_array_type(m, dxil_module_get_float_type(m, 32), 256),
    dxil_module_get_int32_const(m, 1), 4);
  if (!storage.registers || !xe_dxil_test_write(&storage, 0, 0, 15, values, NULL)) return 0;
  io.context = &storage; io.read = xe_dxil_test_read; io.write = xe_dxil_test_write; io.current_block = &block;
  instruction.opcode = 18; instruction.source_swizzle = 4; instruction.destination = 1;
  instruction.destination_swizzle = 1672;
  if (!xe_dxil_emit_texture_fetch(&alu, &instruction, &io, &fetch)) return 0;
  for (i = 0; i < 4; ++i) values[i] = xe_dxil_test_read(&storage, 0, 1, i, NULL);
  return 1;
}
