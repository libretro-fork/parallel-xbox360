/* Native vertex conversion execution fixtures. MSVC C89; BSD license. */
#include "guest_vertex.h"
#include "guest_flow.h"
#include "dxil_test_storage.h"
#include <dxil_module.h>
#include <dxil_function.h>

typedef struct VertexCase {
  unsigned format, signed_value, integer_value, no_zero, endian, swizzle;
  int exponent;
  uint32_t words[4];
  float expected[4];
} VertexCase;
#define XYZW 1672
#define XXXX 0
static const VertexCase cases[] = {
  {6,0,1,0,0,XYZW,0,{0xff804020,0,0,0},{32,64,128,255}},
  {6,1,1,0,0,XYZW,0,{0xff807f00,0,0,0},{0,127,-128,-1}},
  {6,0,0,0,0,XYZW,0,{0xff00ff00,0,0,0},{0,1,0,1}},
  {6,1,0,0,0,XYZW,0,{0x817f8000,0,0,0},{0,-1,1,-1}},
  {6,1,0,1,0,XYZW,0,{0x7f807f80,0,0,0},{-1,1.00000011920928955078125f,-1,1.00000011920928955078125f}},
  {6,0,1,0,1,XYZW,0,{0x44332211,0,0,0},{34,17,68,51}},
  {6,0,1,0,2,XYZW,0,{0x44332211,0,0,0},{68,51,34,17}},
  {6,0,1,0,3,XYZW,0,{0x44332211,0,0,0},{51,68,17,34}},
  {7,0,1,0,0,XYZW,0,{0xc0300801,0,0,0},{1,2,3,3}},
  {7,1,0,0,0,XYZW,0,{0xa007fe00,0,0,0},{-1,1,-1,-1}},
  {16,0,1,0,0,XYZW,0,{0x00c01001,0,0,0},{1,2,3,0}},
  {17,0,1,0,0,XYZW,0,{0x00600801,0,0,0},{1,2,3,0}},
  {25,1,1,0,0,XYZW,0,{0x80007fff,0,0,0},{32767,-32768,0,0}},
  {26,0,1,0,0,XYZW,0,{0x00020001,0x00040003,0,0},{1,2,3,4}},
  {26,1,0,1,0,XYZW,0,{0x7fff8000,0x7fff8000,0,0},{-1,1,-1,1}},
  {31,0,0,0,0,XYZW,0,{0xc0003c00,0,0,0},{1,-2,0,0}},
  {32,0,0,0,0,XYZW,0,{0xc0003c00,0x38004400,0,0},{1,-2,4,0.5f}},
  {33,1,1,0,0,XYZW,0,{0xfffffffe,0,0,0},{-2,0,0,0}},
  {34,0,1,0,0,XYZW,0,{0x1000001,7,0,0},{16777216,7,0,0}},
  {35,1,0,0,0,XYZW,0,{0x80000000,0x7fffffff,0,0,},{-1,1,0,0}},
  {35,0,0,0,0,XYZW,0,{0xffffffff,0,0xffffffff,0},{1,0,1,0}},
  {36,0,0,0,0,XYZW,2,{0x3f800000,0,0,0},{4,0,0,0}},
  {37,0,0,0,0,XYZW,-2,{0x40800000,0xc1000000,0,0},{1,-2,0,0}},
  {38,0,0,0,0,XYZW,0,{0x3f800000,0xc0000000,0x40800000,0x3f000000},{1,-2,4,0.5f}},
  {57,0,0,0,0,XYZW,0,{0x3f800000,0xc0000000,0x40800000,0},{1,-2,4,0}},
  {6,0,1,0,0,XXXX,-32,{0x01010101,0,0,0},{0.00000000023283064365386962890625f,0.00000000023283064365386962890625f,0.00000000023283064365386962890625f,0.00000000023283064365386962890625f}},
  {36,0,0,0,0,XXXX,31,{0x3f800000,0,0,0},{2147483648.0f,2147483648.0f,2147483648.0f,2147483648.0f}},
  /* x, keep, one, zero. Keep is replaced by 42 only in this result fixture. */
  {6,0,1,0,0,2424,0,{0x01010103,0,0,0},{3,42,1,0}}
};

typedef struct VertexMemory {
  InstructionStorage storage;
  XeDxilValue memory, loads, thread;
  XeDxilVertexFetch *fetch;
} VertexMemory;

static int memory_fetch(void *context, const uint32_t *words, unsigned serialized,
                          XeDxilAlu *alu, XeDxilAluIo *io) {
  VertexMemory *f = (VertexMemory *)context;
  XeDxilVertexInstruction instruction;
  (void)serialized;
  return xe_dxil_decode_vertex(words, 3, &instruction) &&
    xe_dxil_emit_vertex_fetch(alu, &instruction, io, f->fetch);
}

static XeDxilValue memory_constant(void *context, unsigned index, unsigned word) {
  VertexMemory *f = (VertexMemory *)context;
  if (index != 95 || word > 1) return NULL;
  return dxil_emit_binop(f->storage.module, DXIL_BINOP_XOR, f->thread,
    dxil_module_get_int32_const(f->storage.module, word ? 12 * 4 : (int32_t)UINT32_C(0x80000010)), 0);
}

static XeDxilValue memory_load(void *context, XeDxilValue address, XeDxilValue in_bounds) {
  VertexMemory *f = (VertexMemory *)context;
  struct dxil_module *m = f->storage.module;
  XeDxilValue args[3], count, safe;
  count = dxil_emit_load(m, f->loads, 4, 0);
  count = count ? dxil_emit_binop(m, DXIL_BINOP_ADD, count, dxil_module_get_int32_const(m, 1), 0) : NULL;
  if (!count || !dxil_emit_store(m, count, f->loads, 4, 0)) return NULL;
  safe = dxil_emit_select(m, in_bounds, address, dxil_module_get_int32_const(m, 0));
  args[0] = f->memory; args[1] = dxil_module_get_int32_const(m, 0); args[2] = safe;
  address = safe ? dxil_emit_gep_inbounds(m, args, 3) : NULL;
  return address ? dxil_emit_load(m, address, 4, 0) : NULL;
}

static int memory_test(struct dxil_module *m, XeDxilValue handle, XeDxilValue thread,
                        unsigned start, FILE *golden) {
  static const float results[15] = {7,8,9,3,3,6,6,6,10,10,0,4,7,0,0};
  static const float loads[15] = {1,2,3,4,4,5,5,6,7,8,9,10,11,12,13};
  VertexMemory fixture;
  XeDxilVertexFetch fetch;
  XeDxilVertexInstruction instruction;
  XeDxilGuestFlow flow;
  XeDxilAlu alu;
  XeDxilAluIo io;
  XeDxilValue values[4], args[3], pointer;
  const struct dxil_type *type;
  uint32_t expected[8];
  unsigned i, j, block = 0;
  static const uint32_t program[] = {
    0x11001, 0x2000, 0,
    (2u << 12) | (1u << 19) | (31u << 20) | (2u << 25),
    (33u << 16) | (1u << 13), 2 | (1u << 8)
  };
  memset(&fixture, 0, sizeof(fixture)); memset(&fetch, 0, sizeof(fetch));
  memset(&io, 0, sizeof(io));
  fixture.storage.module = m; fixture.storage.current_block = &block; fixture.thread = thread;
  type = dxil_module_get_array_type(m, dxil_module_get_float_type(m, 32), 256);
  fixture.storage.registers = dxil_emit_alloca(m, type, dxil_module_get_int32_const(m, 1), 4);
  type = dxil_module_get_array_type(m, dxil_module_get_int_type(m, 32), 32);
  fixture.memory = dxil_emit_alloca(m, type, dxil_module_get_int32_const(m, 1), 4);
  fixture.loads = dxil_emit_alloca(m, dxil_module_get_int_type(m, 32), dxil_module_get_int32_const(m, 1), 4);
  if (!fixture.storage.registers || !fixture.memory || !fixture.loads ||
      !dxil_emit_store(m, dxil_module_get_int32_const(m, 0), fixture.loads, 4, 0)) return 0;
  for (i = 0; i < 32; ++i) {
    args[0] = fixture.memory; args[1] = dxil_module_get_int32_const(m, 0);
    args[2] = dxil_module_get_int32_const(m, (int32_t)i);
    pointer = dxil_emit_gep_inbounds(m, args, 3);
    if (!pointer || !dxil_emit_store(m, args[2], pointer, 4, 0)) return 0;
  }
  for (i = 0; i < 5; ++i) {
    for (j = 0; j < 4; ++j) values[j] = dxil_module_get_float_const(m, i == 2 ? 42.0f : 1.75f);
    if (!xe_dxil_test_write(&fixture.storage, 0, i, 15, values, NULL)) return 0;
  }
  io.context = &fixture.storage; io.read = xe_dxil_test_read; io.write = xe_dxil_test_write;
  io.current_block = &block; io.loop_address = dxil_module_get_int32_const(m, 2);
  fetch.context = &fixture; fetch.constant = memory_constant; fetch.load = memory_load;
  fixture.fetch = &fetch;
  xe_dxil_alu_init(&alu, m);
  if (!xe_dxil_vertex_fetch_init(&alu, &fetch)) return 0;
  memset(&flow, 0, sizeof(flow));
  flow.module = m; flow.alu = &alu; flow.io = &io; flow.context = &fixture;
  flow.words = program; flow.word_count = ARRAY_SIZE(program); flow.control_flow_count = 1;
  flow.fetch = memory_fetch;
  if (!xe_dxil_emit_guest_flow(&flow)) return 0;
  for (j = 0; j < 4; ++j) {
    values[j] = xe_dxil_test_read(&fixture.storage, 0, 2, j, NULL);
    expected[j] = xe_dxil_test_float_bits(7.0f);
  }
  expected[4] = 0; expected[5] = 0; expected[6] = 0; expected[7] = UINT32_C(0x83000000);
  if (!xe_dxil_test_record(m, handle, start++, &alu, values, expected, golden)) return 0;
  /* Keep the following address/predication cases independent of this load. */
  if (!dxil_emit_store(m, dxil_module_get_int32_const(m, 0), fixture.loads, 4, 0)) return 0;
  io.loop_address = dxil_module_get_int32_const(m, 2);
  for (i = 0; i < ARRAY_SIZE(results); ++i) {
    memset(&instruction, 0, sizeof(instruction));
    instruction.constant_index = 95; instruction.format = 33; instruction.integer_value = 1;
    instruction.stride = 2; instruction.destination = 2; instruction.offset = 1;
    instruction.source = 0; instruction.swizzle = XXXX;
    if (i == 1 || i == 5 || i == 7 || i == 9 || i == 13) instruction.mini = 1;
    if (i == 1) instruction.offset = 2;
    if (i == 2) instruction.rounded_index = 1;
    if (i == 3) {
      for (j = 0; j < 4; ++j) values[j] = dxil_module_get_float_const(m, -0.2f);
      if (!xe_dxil_test_write(&fixture.storage, 0, 0, 15, values, NULL)) return 0;
    }
    if (i == 4 || i == 6 || i == 8 || i == 11) {
      for (j = 0; j < 4; ++j) values[j] = dxil_module_get_float_const(m, i == 6 || i == 8 ? 3.0f : 1.75f);
      if (!xe_dxil_test_write(&fixture.storage, 0, 0, 15, values, NULL)) return 0;
    }
    if (i == 4) instruction.swizzle = 4095;
    if (i == 5 || i == 7 || i == 9) instruction.offset = 0;
    if (i == 6 || i == 8) { instruction.predicated = 1; instruction.predicate_condition = i == 6; instruction.offset = 0; }
    if (i == 10 || i == 14) { instruction.stride = 0; instruction.offset = i == 10 ? 20 : 4194303; }
    if (i == 11) instruction.offset = -2;
    if (i == 12) { instruction.source = 1; instruction.source_relative = 1; instruction.destination_relative = 1; }
    if (i == 13) instruction.offset = -4194304;
    if (!xe_dxil_emit_vertex_fetch(&alu, &instruction, &io, &fetch)) return 0;
    for (j = 0; j < 4; ++j) {
      values[j] = xe_dxil_test_read(&fixture.storage, 0, i == 12 ? 4 : 2, j, NULL);
      expected[j] = xe_dxil_test_float_bits(results[i]);
    }
    expected[4] = 0; expected[5] = 0; expected[6] = 0; expected[7] = UINT32_C(0x81000000) | i;
    if (!xe_dxil_test_record(m, handle, start + i * 2, &alu, values, expected, golden)) return 0;
    pointer = dxil_emit_load(m, fixture.loads, 4, 0);
    if (!pointer) return 0;
    values[0] = dxil_emit_cast(m, DXIL_CAST_UITOFP, dxil_module_get_float_type(m, 32), pointer);
    for (j = 0; j < 4; ++j) { values[j] = values[0]; expected[j] = xe_dxil_test_float_bits(loads[i]); }
    expected[7] = UINT32_C(0x82000000) | i;
    if (!xe_dxil_test_record(m, handle, start + i * 2 + 1, &alu, values, expected, golden)) return 0;
  }
  return 1;
}

int xe_dxil_vertex_test(struct dxil_module *m, const char *path) {
  XeDxilVertexInstruction instruction, decoded;
  XeDxilAlu alu;
  XeDxilValue args[5], handle, thread, words[4], values[4], endian;
  const struct dxil_func *function;
  uint32_t expected[8], encoding[3];
  unsigned i, j, mask;
  char *expected_path;
  FILE *golden;
  int result = 0;
  size_t length = strlen(path);
  if (length > (size_t)-1 - 10) return 0;
  encoding[0] = (63u << 5) | (1u << 11) | (62u << 12) | (1u << 18) |
    (31u << 20) | (2u << 25) | (3u << 30);
  encoding[1] = XYZW | (1u << 12) | (1u << 13) | (1u << 14) | (1u << 15) |
    (38u << 16) | (32u << 24) | (1u << 30) | (1u << 31);
  encoding[2] = 255 | (4194304u << 8) | (1u << 31);
  if (!xe_dxil_decode_vertex(encoding, 3, &decoded) || decoded.source != 63 ||
      decoded.destination != 62 || !decoded.source_relative || !decoded.destination_relative ||
      decoded.source_component != 3 || decoded.constant_index != 95 || decoded.stride != 255 ||
      decoded.exponent_adjust != -32 || decoded.offset != -4194304 || !decoded.mini ||
      !decoded.predicated || !decoded.predicate_condition || !decoded.signed_value ||
      !decoded.integer_value || !decoded.signed_no_zero || !decoded.rounded_index ||
      decoded.swizzle != XYZW || decoded.format != 38) return 0;
  encoding[2] = 4194303u << 8; encoding[1] = (31u << 24) | (6u << 16);
  if (!xe_dxil_decode_vertex(encoding, 3, &decoded) || decoded.exponent_adjust != 31 ||
      decoded.offset != 4194303 || xe_dxil_decode_vertex(encoding, 2, &decoded)) return 0;
  encoding[0] |= 1;
  if (xe_dxil_decode_vertex(encoding, 3, &decoded) || xe_dxil_vertex_needed_words(0, XYZW) != -1 ||
      xe_dxil_vertex_needed_words(38, 4096) != -1 || xe_dxil_vertex_needed_words(6, 6) != -1 || xe_dxil_vertex_needed_words(38, 4095) != 0 ||
      xe_dxil_vertex_needed_words(26, 1170) != 2) return 0;
  args[0] = dxil_module_get_int32_const(m, 93); args[1] = dxil_module_get_int32_const(m, 0);
  function = dxil_get_function(m, "dx.op.threadId", DXIL_I32);
  if (!function) return 0;
  thread = dxil_emit_call(m, function, args, 2);
  args[0] = dxil_module_get_int32_const(m, 57);
  args[1] = dxil_module_get_int8_const(m, DXIL_RESOURCE_CLASS_UAV);
  args[2] = dxil_module_get_int32_const(m, 0); args[3] = args[2];
  args[4] = dxil_module_get_int1_const(m, 0);
  function = dxil_get_function(m, "dx.op.createHandle", DXIL_NONE);
  if (!function || !thread) return 0;
  handle = dxil_emit_call(m, function, args, 5);
  if (!handle) return 0;
  expected_path = (char *)malloc(length + 10);
  if (!expected_path) return 0;
  memcpy(expected_path, path, length); memcpy(expected_path + length, ".expected", 10);
  golden = fopen(expected_path, "wb"); free(expected_path);
  if (!golden) return 0;
  for (i = 0; i < ARRAY_SIZE(cases); ++i) {
    xe_dxil_alu_init(&alu, m); memset(&instruction, 0, sizeof(instruction));
    instruction.format = cases[i].format; instruction.swizzle = cases[i].swizzle;
    instruction.signed_value = cases[i].signed_value; instruction.integer_value = cases[i].integer_value;
    instruction.signed_no_zero = cases[i].no_zero; instruction.exponent_adjust = cases[i].exponent;
    for (j = 0; j < 4; ++j) {
      words[j] = dxil_emit_binop(m, DXIL_BINOP_XOR, thread,
        dxil_module_get_int32_const(m, (int32_t)cases[i].words[j]), 0);
      expected[j] = xe_dxil_test_float_bits(cases[i].expected[j]);
    }
    endian = dxil_emit_binop(m, DXIL_BINOP_XOR, thread,
      dxil_module_get_int32_const(m, (int32_t)cases[i].endian), 0);
    if (!xe_dxil_vertex_convert(&alu, &instruction, words, endian, values, &mask)) goto done;
    if (mask != (i + 1 == ARRAY_SIZE(cases) ? 13u : 15u)) goto done;
    for (j = 0; j < 4; ++j) if (!(mask & (1u << j))) values[j] = dxil_module_get_float_const(m, 42.0f);
    expected[4] = 0; expected[5] = 0; expected[6] = 0; expected[7] = UINT32_C(0x80000000) | i;
    if (!xe_dxil_test_record(m, handle, i, &alu, values, expected, golden)) goto done;
  }
  if (!memory_test(m, handle, thread, ARRAY_SIZE(cases), golden)) goto done;
  result = dxil_emit_ret_void(m);
done:
  if (fclose(golden)) result = 0;
  return result;
}
