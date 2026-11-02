/* Native EDRAM format execution fixtures. MSVC C89; BSD license. */
#include "edram_format.h"
#include "guest_resources.h"
#include "dxil_test_storage.h"
#include <dxil_module.h>
#include <dxil_function.h>

typedef struct ColorCase {
  unsigned format, integer_view, words;
  uint32_t source[4], packed[2];
} ColorCase;
static const ColorCase colors[] = {
  {0,0,1,{0x3f800000,0x3f000000,0,0x3f800000},{0xff0080ff,0}},
  {1,0,1,{0x3f800000,0x3f000000,0,0x3f800000},{0xff00bfff,0}},
  {2,0,1,{0x3f800000,0x3f000000,0,0x3f800000},{0xc00803ff,0}},
  {3,0,1,{0x3f800000,0x3f000000,0,0x3f800000},{0xc0040180,0}},
  {4,1,1,{0x7e01,0x8000,0xffff,0xff80},{0x80007e01,0}},
  {5,1,2,{0x7e01,0x8000,0xffff,0xff80},{0x80007e01,0xff80ffff}},
  {6,1,1,{0x7e01,0x8000,0xffff,0xff80},{0x80007e01,0}},
  {7,1,2,{0x7e01,0x8000,0xffff,0xff80},{0x80007e01,0xff80ffff}},
  {10,0,1,{0x3f800000,0x3f000000,0,0x3f800000},{0xc00803ff,0}},
  {12,0,1,{0x3f800000,0x3f000000,0,0x3f800000},{0xc0040180,0}},
  {14,1,1,{0x7fc12345,0x80000000,0,0},{0x7fc12345,0}},
  {15,1,2,{0x7fc12345,0x80000000,0,0},{0x7fc12345,0x80000000}},
  {15,0,2,{0x7fc12345,0x80000000,0,0},{0x7fc12345,0x80000000}}
};
typedef struct FloatCase { uint32_t bits, packed; } FloatCase;
static const FloatCase small_floats[] = {
  {0,0},{0x80000000,0},{0x3b000000,1},{0x3e7e0000,127},
  {0x3e800000,128},{0x3f000000,256},{0x3f800000,384},{0x41ff0000,1023},
  {0x7f800000,1023},{0xff800000,0},{0x7fc12345,0},
  {0x3f808000,384},{0x3f818000,386}
};
typedef struct DepthCase {
  uint32_t bits, truncated, rounded, truncated_bits, rounded_bits;
  int remap;
} DepthCase;
static const DepthCase depths[] = {
  {0,0,0,0,0,0},
  {0x2e800000,1,1,0x2e800000,0x2e800000,0},
  {0x2e000000,0,0,0,0,0},
  /* CFloat24 drops shifted-out denormal bits before the final rounding. */
  {0x2e000001,0,0,0,0,0},
  {0x2e200000,0,1,0,0x2e800000,0},
  {0x38800000,0x100000,0x100000,0x38800000,0x38800000,0},
  {0x3f000000,0xe00000,0xe00000,0x3f000000,0x3f000000,0},
  {0x3f000004,0xe00000,0xe00000,0x3f000000,0x3f000000,0},
  {0x3f00000c,0xe00001,0xe00002,0x3f000008,0x3f000010,0},
  {0x3ffffff8,0xffffff,0xffffff,0x3ffffff8,0x3ffffff8,0},
  {0x2e000000,1,1,0x2e000000,0x2e000000,1},
  {0x38000000,0x100000,0x100000,0x38000000,0x38000000,1},
  {0x3f000000,0xf00000,0xf00000,0x3f000000,0x3f000000,1},
  {0x3f7ffff8,0xffffff,0xffffff,0x3f7ffff8,0x3f7ffff8,1}
};
static XeDxilValue as_float(struct dxil_module *m, XeDxilValue value) {
  return value ? dxil_emit_cast(m, DXIL_CAST_BITCAST, dxil_module_get_float_type(m, 32), value) : NULL;
}
static int exhaustive_float7e3(struct dxil_module *m, XeDxilAlu *alu, XeDxilValue handle,
                                 unsigned start, FILE *golden) {
  XeDxilValue counter, index, value, args[9], address, condition;
  const struct dxil_func *store;
  uint32_t expected[8];
  unsigned blocks, i, j;
  counter = dxil_emit_alloca(m, dxil_module_get_int_type(m, 32), dxil_module_get_int32_const(m, 1), 4);
  if (!counter || !dxil_emit_store(m, dxil_module_get_int32_const(m, 0), counter, 4, 0) ||
      !dxil_function_reserve_blocks(m, 3, &blocks) || !dxil_emit_branch(m, NULL, blocks, 0)) return 0;
  m->cur_emitting_func->basic_block_ids[blocks] = (int)m->cur_emitting_func->curr_block;
  index = dxil_emit_load(m, counter, 4, 0);
  condition = index ? dxil_emit_cmp(m, DXIL_ICMP_ULT, index, dxil_module_get_int32_const(m, 1024)) : NULL;
  if (!condition || !dxil_emit_branch(m, condition, blocks + 1, blocks + 2)) return 0;
  m->cur_emitting_func->basic_block_ids[blocks + 1] = (int)m->cur_emitting_func->curr_block;
  value = xe_dxil_unpack_float7e3(alu, index);
  value = xe_dxil_pack_float7e3(alu, value);
  address = dxil_emit_binop(m, DXIL_BINOP_ADD, index, dxil_module_get_int32_const(m, (int32_t)start), 0);
  address = address ? dxil_emit_binop(m, DXIL_BINOP_MUL, address, dxil_module_get_int32_const(m, 32), 0) : NULL;
  store = dxil_get_function(m, "dx.op.bufferStore", DXIL_I32);
  if (!value || !address || !store) return 0;
  args[0] = dxil_module_get_int32_const(m, 69); args[1] = handle;
  args[3] = dxil_module_get_undef(m, dxil_module_get_int_type(m, 32));
  args[5] = args[3]; args[6] = args[3]; args[7] = args[3]; args[8] = dxil_module_get_int8_const(m, 1);
  for (i = 0; i < 8; ++i) {
    args[2] = dxil_emit_binop(m, DXIL_BINOP_ADD, address, dxil_module_get_int32_const(m, (int32_t)(i * 4)), 0);
    args[4] = i < 4 ? value : i == 7 ? dxil_emit_binop(m, DXIL_BINOP_OR, index,
      dxil_module_get_int32_const(m, (int32_t)UINT32_C(0xb3000000)), 0) : dxil_module_get_int32_const(m, 0);
    if (!args[2] || !args[4] || !dxil_emit_call_void(m, store, args, 9)) return 0;
  }
  value = dxil_emit_binop(m, DXIL_BINOP_ADD, index, dxil_module_get_int32_const(m, 1), 0);
  if (!value || !dxil_emit_store(m, value, counter, 4, 0) || !dxil_emit_branch(m, NULL, blocks, 0)) return 0;
  m->cur_emitting_func->basic_block_ids[blocks + 2] = (int)m->cur_emitting_func->curr_block;
  memset(expected, 0, sizeof(expected));
  for (i = 0; i < 1024; ++i) {
    for (j = 0; j < 4; ++j) expected[j] = i;
    expected[7] = UINT32_C(0xb3000000) | i;
    if (fwrite(expected, 1, sizeof(expected), golden) != sizeof(expected)) return 0;
  }
  return 1;
}
int xe_dxil_edram_format_test(struct dxil_module *m, const char *path) {
  XeDxilAlu alu;
  XeDxilValue handle, source[4], packed[2], values[4], unpacked[4];
  XeDxilValue value;
  uint32_t expected[8];
  unsigned i, j, index = 0, count;
  size_t length = strlen(path);
  char *name;
  FILE *golden;
  int result = 0;
  if (length > (size_t)-1 - 10) return 0;
  name = (char *)malloc(length + 10);
  if (!name) return 0;
  memcpy(name, path, length); memcpy(name + length, ".expected", 10);
  golden = fopen(name, "wb"); free(name);
  if (!golden) return 0;
  xe_dxil_alu_init(&alu, m);
  handle = xe_dxil_resource_handle(&alu, XE_DXIL_RESOURCE_UAV, 0, 0);
  if (!handle) goto done;
  memset(expected, 0, sizeof(expected));
  for (i = 0; i < ARRAY_SIZE(colors); ++i) {
    for (j = 0; j < 4; ++j) source[j] = colors[i].integer_view ?
      dxil_module_get_int32_const(m, (int32_t)colors[i].source[j]) : xe_dxil_alu_float_bits(&alu, colors[i].source[j]);
    count = (unsigned)xe_dxil_pack_edram_color(&alu, colors[i].format, colors[i].integer_view, source, packed);
    if (count != colors[i].words) goto done;
    for (j = 0; j < 4; ++j) {
      values[j] = j < 2 ? as_float(m, packed[j]) : dxil_module_get_float_const(m, 0.0f);
      expected[j] = j < 2 ? colors[i].packed[j] : 0;
    }
    expected[7] = UINT32_C(0xb0000000) | i;
    if (!xe_dxil_test_record(m, handle, index++, &alu, values, expected, golden)) goto done;
    if (colors[i].integer_view || colors[i].format == 3 || colors[i].format == 12) {
      if (xe_dxil_unpack_edram_color(&alu, colors[i].format, colors[i].integer_view, packed, unpacked) != (int)count) goto done;
      for (j = 0; j < 4; ++j) {
        values[j] = colors[i].integer_view ? as_float(m, unpacked[j]) : unpacked[j];
        expected[j] = colors[i].source[j];
        if ((colors[i].format == 4 || colors[i].format == 6 || colors[i].format == 15) && j >= 2) expected[j] = 0;
        if (colors[i].format == 14 && j) expected[j] = 0;
      }
      expected[7] = UINT32_C(0xb1000000) | i;
      if (!xe_dxil_test_record(m, handle, index++, &alu, values, expected, golden)) goto done;
    }
  }
  for (i = 0; i < ARRAY_SIZE(small_floats); ++i) {
    value = xe_dxil_pack_float7e3(&alu, xe_dxil_alu_float_bits(&alu, small_floats[i].bits));
    for (j = 0; j < 4; ++j) { values[j] = as_float(m, value); expected[j] = small_floats[i].packed; }
    expected[7] = UINT32_C(0xb2000000) | i;
    if (!xe_dxil_test_record(m, handle, index++, &alu, values, expected, golden)) goto done;
  }
  /* Exhaust all 1024 7e3 encodings: the decoder must recover the exact value
   * and the ties-to-even encoder must return the original bits. */
  if (!exhaustive_float7e3(m, &alu, handle, index, golden)) goto done;
  index += 1024;
  for (i = 0; i < ARRAY_SIZE(depths); ++i) {
    value = xe_dxil_alu_float_bits(&alu, depths[i].bits);
    packed[0] = xe_dxil_pack_depth20e4(&alu, value, 0, depths[i].remap);
    packed[1] = xe_dxil_pack_depth20e4(&alu, value, 1, depths[i].remap);
    values[0] = as_float(m, packed[0]); values[1] = as_float(m, packed[1]);
    values[2] = xe_dxil_unpack_depth20e4(&alu, packed[0], depths[i].remap);
    values[3] = xe_dxil_unpack_depth20e4(&alu, packed[1], depths[i].remap);
    expected[0] = depths[i].truncated; expected[1] = depths[i].rounded;
    expected[2] = depths[i].truncated_bits; expected[3] = depths[i].rounded_bits;
    expected[7] = UINT32_C(0xb4000000) | i;
    if (!xe_dxil_test_record(m, handle, index++, &alu, values, expected, golden)) goto done;
  }
  result = dxil_emit_ret_void(m);
done:
  if (fclose(golden)) result = 0;
  return result;
}
