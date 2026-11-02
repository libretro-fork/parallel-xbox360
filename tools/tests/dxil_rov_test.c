/* Ordered-access and depth/stencil fixtures. MSVC C89; BSD license. */
#include "shader_module.h"
#include "guest_resources.h"
#include "edram_depth_stencil.h"
#include <dxil_module.h>
#include <dxil_function.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static XeDxilValue ic(struct dxil_module *m, uint32_t value) { return dxil_module_get_int32_const(m, (int32_t)value); }
static XeDxilValue add(struct dxil_module *m, XeDxilValue a, XeDxilValue b) { return dxil_emit_binop(m, DXIL_BINOP_ADD, a, b, 0); }
static XeDxilValue mul(struct dxil_module *m, XeDxilValue a, XeDxilValue b) { return dxil_emit_binop(m, DXIL_BINOP_MUL, a, b, 0); }
static int write_file(const char *path, const char *suffix, const void *data, size_t size) {
  size_t length = strlen(path), extra = strlen(suffix) + 1;
  char *name = (char *)malloc(length + extra);
  FILE *file;
  int result;
  if (!name) return 0;
  memcpy(name, path, length); memcpy(name + length, suffix, extra);
  file = fopen(name, "wb"); free(name); if (!file) return 0;
  result = fwrite(data, 1, size, file) == size;
  if (fclose(file)) result = 0;
  return result;
}
static int ordered_pixel(XeDxilShaderModule *shader, XeDxilAlu *alu) {
  struct dxil_module *m = alu->module;
  const struct dxil_func *load;
  XeDxilValue args[5], xy[2], address, handle, value;
  unsigned i;
  if (xe_dxil_shader_signature(shader, 0, "SV_Position", 0, DXIL_SEM_POSITION,
      DXIL_PROG_SEM_POSITION, 0, 0, 4, DXIL_INTERP_LINEAR_NOPERSPECTIVE) < 0 ||
      xe_dxil_shader_ordered_buffer(shader, 0, 1) != 0) return 0;
  load = dxil_get_function(m, "dx.op.loadInput", DXIL_F32);
  args[0] = ic(m, 4); args[1] = ic(m, 0); args[2] = args[1];
  args[4] = dxil_module_get_undef(m, dxil_module_get_int_type(m, 32));
  if (!load) return 0;
  for (i = 0; i < 2; ++i) {
    args[3] = dxil_module_get_int8_const(m, (int8_t)i);
    xy[i] = dxil_emit_call(m, load, args, 5);
    xy[i] = xy[i] ? dxil_emit_cast(m, DXIL_CAST_FPTOUI, dxil_module_get_int_type(m, 32), xy[i]) : NULL;
    if (!xy[i]) return 0;
  }
  address = mul(m, add(m, mul(m, xy[1], ic(m, 8)), xy[0]), ic(m, 4));
  handle = xe_dxil_resource_handle(alu, XE_DXIL_RESOURCE_UAV, 0, 1);
  value = xe_dxil_raw_buffer_load(alu, handle, address);
  value = value ? add(m, mul(m, value, ic(m, 3)), ic(m, 1)) : NULL;
  return value && xe_dxil_raw_buffer_store(alu, handle, address, value);
}
static unsigned host_compare(unsigned op, uint32_t a, uint32_t b) {
  switch (op) {
  case 0: return 0; case 1: return a < b; case 2: return a == b;
  case 3: return a <= b; case 4: return a > b; case 5: return a != b;
  case 6: return a >= b; default: return 1;
  }
}
static uint32_t host_stencil(unsigned op, uint32_t old, uint32_t reference) {
  switch (op) {
  case 0: return old; case 1: return 0; case 2: return reference;
  case 3: return old == 255 ? old : old + 1;
  case 4: return old ? old - 1 : old;
  case 5: return old ^ 255; case 6: return (old + 1) & 255;
  default: return (old - 1) & 255;
  }
}
static unsigned make_cases(const char *path) {
  static const uint32_t stencil[] = {0,1,127,255};
  static const uint32_t depths[] = {0,1,0x7fffff,0xffffff};
  static const uint32_t reads[] = {0,255,15,240}, writes[] = {0,255,85,170};
  const unsigned count = 8*8*8*8*12;
  uint32_t *input = (uint32_t *)malloc((size_t)count*13*4);
  uint32_t *golden = (uint32_t *)calloc((size_t)count*8, 4);
  uint32_t constants[4] = {0,0,0,0};
  uint32_t *row, *out, old, depth, changed_stencil, packed;
  unsigned df, sf, op, flags, pattern, n = 0, sp, dp, selected;
  int result;
  if (!input || !golden) { free(input); free(golden); return 0; }
  for (df = 0; df < 8; ++df) for (sf = 0; sf < 8; ++sf) for (op = 0; op < 8; ++op)
    for (flags = 0; flags < 8; ++flags) for (pattern = 0; pattern < 12; ++pattern) {
      row = input + n*13; out = golden + n*8;
      old = stencil[pattern % 4]; depth = depths[pattern % 4];
      row[0] = (depth << 8) | old;
      row[1] = (depth + pattern/4 - 1) & 0xffffff;
      row[2] = flags & 1; row[3] = (flags >> 1) & 1; row[4] = df;
      row[5] = (flags >> 2) & 1; row[6] = sf;
      row[7] = stencil[(pattern + pattern/4) % 4];
      row[8] = reads[pattern % 4]; row[9] = writes[(pattern + pattern/4) % 4];
      row[10] = op; row[11] = (op+3) % 8; row[12] = (op+5) % 8;
      sp = !row[5] || host_compare(sf, row[7] & row[8], old & row[8]);
      dp = !row[2] || host_compare(df, row[1], depth);
      selected = !sp ? row[10] : !dp ? row[11] : row[12];
      changed_stencil = host_stencil(selected, old, row[7]);
      changed_stencil = row[5] ? ((changed_stencil & row[9]) | (old & (255 ^ row[9]))) : old;
      packed = ((sp && dp && row[2] && row[3] ? row[1] : depth) << 8) | changed_stencil;
      out[0] = sp && dp; out[1] = !sp; out[2] = sp && !dp; out[3] = packed != row[0]; out[4] = packed;
      ++n;
    }
  constants[0] = count;
  result = write_file(path, ".memory", input, (size_t)count*13*4) &&
    write_file(path, ".expected", golden, (size_t)count*8*4) && write_file(path, ".constants", constants, sizeof(constants));
  free(input); free(golden); return result ? count : 0;
}
static int depth_stencil_compute(XeDxilShaderModule *shader, XeDxilAlu *alu, const char *path) {
  struct dxil_module *m = alu->module;
  const struct dxil_func *thread;
  XeDxilDepthStencilState state;
  XeDxilDepthStencilResult result;
  XeDxilValue source, destination, constants, count, storage, index, address, words[13], outputs[8], args[2], value;
  unsigned blocks, i;
  if (!make_cases(path) || !xe_dxil_shader_threads(shader, 64, 1, 1) ||
      xe_dxil_shader_resource(shader, XE_DXIL_RESOURCE_UAV, DXIL_RESOURCE_KIND_RAW_BUFFER, 0, 0, 1, 0) != 0 ||
      xe_dxil_shader_resource(shader, XE_DXIL_RESOURCE_SRV, DXIL_RESOURCE_KIND_RAW_BUFFER, 0, 0, 1, 0) != 0 ||
      xe_dxil_shader_resource(shader, XE_DXIL_RESOURCE_CBV, DXIL_RESOURCE_KIND_CBUFFER, 1, 4, 1, 16) != 0) return 0;
  source = xe_dxil_resource_handle(alu, XE_DXIL_RESOURCE_SRV, 0, 0);
  destination = xe_dxil_resource_handle(alu, XE_DXIL_RESOURCE_UAV, 0, 0);
  constants = xe_dxil_resource_handle(alu, XE_DXIL_RESOURCE_CBV, 0, 4);
  count = xe_dxil_cbuffer_word(alu, constants, ic(m, 0));
  thread = dxil_get_function(m, "dx.op.threadId", DXIL_I32);
  args[0] = ic(m, 93); args[1] = ic(m, 0);
  value = thread ? dxil_emit_call(m, thread, args, 2) : NULL;
  storage = dxil_emit_alloca(m, dxil_module_get_int_type(m, 32), ic(m, 1), 4);
  if (!value || !storage || !count || !dxil_emit_store(m, value, storage, 4, 0) ||
      !dxil_function_reserve_blocks(m, 3, &blocks) || !dxil_emit_branch(m, NULL, blocks, 0)) return 0;
  m->cur_emitting_func->basic_block_ids[blocks] = (int)m->cur_emitting_func->curr_block;
  index = dxil_emit_load(m, storage, 4, 0);
  value = index ? dxil_emit_cmp(m, DXIL_ICMP_ULT, index, count) : NULL;
  if (!value || !dxil_emit_branch(m, value, blocks+1, blocks+2)) return 0;
  m->cur_emitting_func->basic_block_ids[blocks+1] = (int)m->cur_emitting_func->curr_block;
  address = mul(m, index, ic(m, 52));
  for (i = 0; i < 13; ++i) words[i] = xe_dxil_raw_buffer_load(alu, source, add(m, address, ic(m, i*4)));
  state.depth_enabled = words[2]; state.depth_write = words[3]; state.depth_function = words[4];
  state.stencil_enabled = words[5]; state.stencil_function = words[6]; state.reference = words[7];
  state.read_mask = words[8]; state.write_mask = words[9]; state.stencil_fail = words[10];
  state.depth_fail = words[11]; state.depth_pass = words[12];
  if (!xe_dxil_edram_depth_stencil(alu, &state, words[0], words[1], &result)) return 0;
  outputs[0] = result.passed; outputs[1] = result.stencil_failed;
  outputs[2] = result.depth_failed; outputs[3] = result.changed;
  for (i = 0; i < 4; ++i) outputs[i] = dxil_emit_cast(m, DXIL_CAST_ZEXT, dxil_module_get_int_type(m, 32), outputs[i]);
  outputs[4] = result.packed; for (i = 5; i < 8; ++i) outputs[i] = ic(m, 0);
  address = mul(m, index, ic(m, 32));
  for (i = 0; i < 8; ++i) if (!xe_dxil_raw_buffer_store(alu, destination, add(m, address, ic(m, i*4)), outputs[i])) return 0;
  if (!dxil_emit_store(m, add(m, index, ic(m, 64)), storage, 4, 0) || !dxil_emit_branch(m, NULL, blocks, 0)) return 0;
  m->cur_emitting_func->basic_block_ids[blocks+2] = (int)m->cur_emitting_func->curr_block;
  return 1;
}
int main(int argc, char **argv) {
  XeDxilShaderModule *shader;
  XeDxilShaderBinary binary;
  XeDxilAlu alu;
  unsigned compute;
  int result = 1;
  if (argc != 3) return 2;
  compute = (unsigned)strtoul(argv[2], NULL, 10); if (compute > 1) return 2;
  memset(&binary, 0, sizeof(binary));
  shader = xe_dxil_shader_create(compute ? DXIL_COMPUTE_SHADER : DXIL_PIXEL_SHADER);
  if (!shader) return 1;
  xe_dxil_alu_init(&alu, xe_dxil_shader_module(shader));
  if (!(compute ? depth_stencil_compute(shader, &alu, argv[1]) : ordered_pixel(shader, &alu))) goto done;
  if (!alu.failed && dxil_emit_ret_void(alu.module) && xe_dxil_shader_finish(shader, &binary))
    result = !write_file(argv[1], "", binary.data, binary.size);
done:
  xe_dxil_shader_binary_free(&binary); xe_dxil_shader_destroy(shader); return result;
}
