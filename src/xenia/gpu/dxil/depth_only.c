/* Native host-depth pixel shaders. MSVC C89; BSD license.
 * Float24 conversion and counter layout follow Xenia's render-backend path. */
#include "stage_layout.h"
#include "depth_only.h"
#include "edram_format.h"
#include "guest_resources.h"
#include <dxil_module.h>
#include <dxil_function.h>

static XeDxilValue integer(struct dxil_module *m, uint32_t value) {
  return dxil_module_get_int32_const(m, (int32_t)value);
}
static XeDxilValue intrinsic(struct dxil_module *m, const char *name,
    enum overload_type overload, XeDxilValue *args, unsigned count) {
  const struct dxil_func *function = dxil_get_function(m, name, overload);
  unsigned i;
  for (i = 0; i < count; ++i) if (!args[i]) return NULL;
  return function ? dxil_emit_call(m, function, args, count) : NULL;
}
int xe_dxil_build_depth_only(const XeDxilDepthOnlyOptions *o, XeDxilShaderBinary *binary) {
  XeDxilShaderModule *shader;
  struct dxil_module *m;
  const struct dxil_func *store;
  XeDxilAlu alu;
  XeDxilValue args[7], depth, bits, exponent, shift, mask, representable;
  XeDxilValue counter, index, coverage, samples, enabled, sample;
  unsigned blocks, body, done;
  int output, result = 0;
  if (!o || !binary || o->depth_mode > 2 || (o->count_coverage &&
      (!o->counter_stride_bytes || (o->counter_stride_bytes & 3) ||
       o->system_constants_bytes < 4 || o->counter_index_word >= o->system_constants_bytes / 4))) return 0;
  shader = xe_dxil_shader_create(DXIL_PIXEL_SHADER);
  if (!shader) return 0;
  m = xe_dxil_shader_module(shader); xe_dxil_alu_init(&alu, m);
  if (o->depth_mode) {
    if (xe_dxil_shader_signature(shader, 0, "SV_Position", 0, DXIL_SEM_POSITION,
        DXIL_PROG_SEM_POSITION, o->guest_linkage ? XE_DXIL_GUEST_POSITION_ROW : 0, 0, 4, DXIL_INTERP_LINEAR_NOPERSPECTIVE) < 0 ||
        !xe_dxil_shader_sample_frequency(shader)) goto finish;
    output = xe_dxil_shader_signature(shader, 1, o->depth_mode == 1 ? "SV_DepthLessEqual" : "SV_Depth", 0,
      o->depth_mode == 1 ? DXIL_SEM_DEPTH_LE : DXIL_SEM_DEPTH,
      o->depth_mode == 1 ? DXIL_PROG_SEM_DEPTH_LE : DXIL_PROG_SEM_DEPTH, UINT32_MAX, 0, 1, 0);
    if (output < 0) goto finish;
    args[0] = integer(m, 4); args[1] = integer(m, 0); args[2] = args[1];
    args[3] = dxil_module_get_int8_const(m, 2);
    args[4] = dxil_module_get_undef(m, dxil_module_get_int_type(m, 32));
    depth = intrinsic(m, "dx.op.loadInput", DXIL_F32, args, 5);
    if (!depth) goto finish;
    depth = xe_dxil_alu_saturate(&alu, dxil_emit_binop(m, DXIL_BINOP_MUL, depth,
      dxil_module_get_float_const(m, 2.0f), 0));
    if (o->depth_mode == 2) {
      bits = xe_dxil_pack_depth20e4(&alu, depth, 1, 0);
      depth = xe_dxil_unpack_depth20e4(&alu, bits, 1);
    } else {
      bits = dxil_emit_cast(m, DXIL_CAST_BITCAST, dxil_module_get_int_type(m, 32), depth);
      if (!bits) goto finish;
      exponent = dxil_emit_binop(m, DXIL_BINOP_LSHR, bits, integer(m, 23), 0);
      shift = dxil_emit_binop(m, DXIL_BINOP_SUB, integer(m, 116), exponent, 0);
      if (!exponent || !shift) goto finish;
      shift = dxil_emit_select(m, dxil_emit_cmp(m, DXIL_ICMP_SGT, shift, integer(m, 3)), shift, integer(m, 3));
      if (!shift) goto finish;
      /* Bound the shift even on the unselected underflow path. */
      shift = dxil_emit_select(m, dxil_emit_cmp(m, DXIL_ICMP_ULT, shift, integer(m, 32)), shift, integer(m, 31));
      mask = dxil_emit_binop(m, DXIL_BINOP_SHL, integer(m, UINT32_MAX), shift, 0);
      bits = mask ? dxil_emit_binop(m, DXIL_BINOP_AND, bits, mask, 0) : NULL;
      representable = dxil_emit_cmp(m, DXIL_FCMP_OGE, depth, dxil_module_get_float_const(m, 5.82076609134674072265625e-11f));
      if (!bits || !representable) goto finish;
      depth = dxil_emit_cast(m, DXIL_CAST_BITCAST, dxil_module_get_float_type(m, 32), bits);
      depth = depth ? dxil_emit_binop(m, DXIL_BINOP_MUL, depth, dxil_module_get_float_const(m, 0.5f), 0) : NULL;
      depth = depth ? dxil_emit_select(m, representable, depth, dxil_module_get_float_const(m, 0.0f)) : NULL;
    }
    args[0] = integer(m, 5); args[1] = integer(m, (unsigned)output); args[2] = integer(m, 0);
    args[3] = dxil_module_get_int8_const(m, 0); args[4] = depth;
    store = dxil_get_function(m, "dx.op.storeOutput", DXIL_F32);
    if (!depth || !store || !dxil_emit_call_void(m, store, args, 5)) goto finish;
  }
  if (o->count_coverage) {
    if (xe_dxil_shader_resource(shader, XE_DXIL_RESOURCE_CBV, DXIL_RESOURCE_KIND_CBUFFER,
          1, 0, 1, o->system_constants_bytes) != 0 ||
        xe_dxil_shader_resource(shader, XE_DXIL_RESOURCE_UAV, DXIL_RESOURCE_KIND_RAW_BUFFER, 0, 1, 1, 0) != 0) goto finish;
    counter = xe_dxil_resource_handle(&alu, XE_DXIL_RESOURCE_UAV, 0, 1);
    index = xe_dxil_cbuffer_word(&alu, xe_dxil_resource_handle(&alu, XE_DXIL_RESOURCE_CBV, 0, 0), integer(m, o->counter_index_word));
    args[0] = integer(m, 91); coverage = intrinsic(m, "dx.op.coverage", DXIL_I32, args, 1);
    if (!counter || !index || !coverage) goto finish;
    if (o->depth_mode) {
      args[0] = integer(m, 90); sample = intrinsic(m, "dx.op.sampleIndex", DXIL_I32, args, 1);
      mask = sample ? dxil_emit_binop(m, DXIL_BINOP_SHL, integer(m, 1), sample, 0) : NULL;
      coverage = mask ? dxil_emit_binop(m, DXIL_BINOP_AND, coverage, mask, 0) : NULL;
    }
    args[0] = integer(m, 31); args[1] = coverage;
    samples = intrinsic(m, "dx.op.unaryBits", DXIL_I32, args, 2);
    enabled = dxil_emit_cmp(m, DXIL_ICMP_NE, index, integer(m, UINT32_MAX));
    if (!samples || !enabled || !dxil_function_reserve_blocks(m, 2, &blocks)) goto finish;
    body = blocks; done = blocks + 1;
    if (!dxil_emit_branch(m, enabled, body, done)) goto finish;
    m->cur_emitting_func->basic_block_ids[body] = (int)m->cur_emitting_func->curr_block;
    args[0] = integer(m, 78); args[1] = counter; args[2] = integer(m, 0);
    args[3] = dxil_emit_binop(m, DXIL_BINOP_MUL, index, integer(m, o->counter_stride_bytes), 0);
    args[4] = integer(m, 0); args[5] = args[4]; args[6] = samples;
    if (!intrinsic(m, "dx.op.atomicBinOp", DXIL_I32, args, 7) || !dxil_emit_branch(m, NULL, done, 0)) goto finish;
    m->cur_emitting_func->basic_block_ids[done] = (int)m->cur_emitting_func->curr_block;
  }
  if (!alu.failed && dxil_emit_ret_void(m)) result = xe_dxil_shader_finish(shader, binary);
finish:
  xe_dxil_shader_destroy(shader);
  return result;
}
void xe_dxil_depth_only_set_free(XeDxilShaderBinary binaries[3][2]) {
  unsigned mode, count;
  if (!binaries) return;
  for (mode = 0; mode < 3; ++mode) for (count = 0; count < 2; ++count)
    xe_dxil_shader_binary_free(&binaries[mode][count]);
}
int xe_dxil_build_depth_only_set(const XeDxilDepthOnlyOptions *base,
    unsigned float24, unsigned coverage, XeDxilShaderBinary binaries[3][2]) {
  XeDxilDepthOnlyOptions options;
  unsigned mode, count;
  if (!base || !binaries) return 0;
  for (mode = 0; mode < 3; ++mode) for (count = 0; count < 2; ++count)
    if (binaries[mode][count].data || binaries[mode][count].size) return 0;
  options = *base;
  for (mode = 0; mode < (float24 ? 3u : 1u); ++mode) {
    for (count = 0; count < (coverage ? 2u : 1u); ++count) {
      options.depth_mode = mode; options.count_coverage = count;
      if (!xe_dxil_build_depth_only(&options, &binaries[mode][count])) {
        xe_dxil_depth_only_set_free(binaries); return 0;
      }
    }
  }
  return 1;
}
