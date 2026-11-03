/* Native depth-only EDRAM pixel shaders. MSVC C89; BSD license.
 * Layout, sample locations and depth/stencil rules follow Xenia's FSI path. */
#include "stage_layout.h"
#include "rov_depth.h"
#include "edram_depth_stencil.h"
#include "edram_format.h"
#include "guest_resources.h"
#include <dxil_module.h>
#include <dxil_function.h>
typedef struct Rov {
  XeDxilShaderModule *shader;
  XeDxilAlu alu;
  XeDxilValue constants;
} Rov;
static XeDxilValue ic(Rov *r, uint32_t v) { return dxil_module_get_int32_const(r->alu.module, (int32_t)v); }
static XeDxilValue fc(Rov *r, float v) { return dxil_module_get_float_const(r->alu.module, v); }
static XeDxilValue bin(Rov *r, enum dxil_bin_opcode op, XeDxilValue a, XeDxilValue b) {
  XeDxilValue v = a && b ? dxil_emit_binop(r->alu.module, op, a, b, 0) : NULL;
  if (!v) r->alu.failed = 1;
  return v;
}
static XeDxilValue cmp(Rov *r, enum dxil_cmp_pred op, XeDxilValue a, XeDxilValue b) {
  XeDxilValue v = a && b ? dxil_emit_cmp(r->alu.module, op, a, b) : NULL;
  if (!v) r->alu.failed = 1;
  return v;
}
static XeDxilValue pick(Rov *r, XeDxilValue c, XeDxilValue a, XeDxilValue b) {
  XeDxilValue v = c && a && b ? dxil_emit_select(r->alu.module, c, a, b) : NULL;
  if (!v) r->alu.failed = 1;
  return v;
}
static XeDxilValue cast(Rov *r, enum dxil_cast_opcode op, int floating, XeDxilValue v) {
  v = v ? dxil_emit_cast(r->alu.module, op, floating ? dxil_module_get_float_type(r->alu.module, 32) :
    dxil_module_get_int_type(r->alu.module, 32), v) : NULL;
  if (!v) r->alu.failed = 1;
  return v;
}
static XeDxilValue field(Rov *r, XeDxilValue v, unsigned shift, unsigned mask) {
  return bin(r, DXIL_BINOP_AND, bin(r, DXIL_BINOP_LSHR, v, ic(r, shift)), ic(r, mask));
}
static XeDxilValue flag(Rov *r, XeDxilValue v, unsigned mask) {
  return cmp(r, DXIL_ICMP_NE, bin(r, DXIL_BINOP_AND, v, ic(r, mask)), ic(r, 0));
}
static XeDxilValue word(Rov *r, unsigned index) { return xe_dxil_cbuffer_word(&r->alu, r->constants, ic(r, index)); }
static XeDxilValue call(Rov *r, const char *name, enum overload_type type, XeDxilValue *args, unsigned count) {
  const struct dxil_func *function = dxil_get_function(r->alu.module, name, type);
  XeDxilValue value;
  unsigned i;
  for (i = 0; i < count; ++i) if (!args[i]) { r->alu.failed = 1; return NULL; }
  value = function ? dxil_emit_call(r->alu.module, function, args, count) : NULL;
  if (!value) r->alu.failed = 1;
  return value;
}
static XeDxilValue unary(Rov *r, unsigned opcode, XeDxilValue value) {
  XeDxilValue args[2]; args[0] = ic(r, opcode); args[1] = value;
  return call(r, "dx.op.unary", DXIL_F32, args, 2);
}
static void begin(Rov *r, unsigned block) {
  r->alu.module->cur_emitting_func->basic_block_ids[block] = (int)r->alu.module->cur_emitting_func->curr_block;
}
static int conditional(Rov *r, XeDxilValue condition, unsigned *done) {
  unsigned blocks;
  if (!condition || !dxil_function_reserve_blocks(r->alu.module, 2, &blocks) ||
      !dxil_emit_branch(r->alu.module, condition, blocks, blocks+1)) return 0;
  begin(r, blocks); *done = blocks+1; return 1;
}
static int end(Rov *r, unsigned done) {
  if (!dxil_emit_branch(r->alu.module, NULL, done, 0)) return 0;
  begin(r, done); return 1;
}
static XeDxilValue quantize(Rov *r, XeDxilValue float24, XeDxilValue depth) {
  XeDxilValue incoming[2];
  struct dxil_instr *phi;
  unsigned blocks, predecessors[2];
  if (!float24 || !depth || !dxil_function_reserve_blocks(r->alu.module, 3, &blocks) ||
      !dxil_emit_branch(r->alu.module, float24, blocks, blocks+1)) return NULL;
  begin(r, blocks);
  incoming[0] = xe_dxil_pack_depth20e4(&r->alu, depth, 1, 0); predecessors[0] = blocks;
  if (!incoming[0] || !dxil_emit_branch(r->alu.module, NULL, blocks+2, 0)) return NULL;
  begin(r, blocks+1);
  incoming[1] = cast(r, DXIL_CAST_FPTOUI, 0,
    unary(r, 26, bin(r, DXIL_BINOP_MUL, depth, fc(r, 16777215.0f))));
  predecessors[1] = blocks+1;
  if (!incoming[1] || !dxil_emit_branch(r->alu.module, NULL, blocks+2, 0)) return NULL;
  begin(r, blocks+2);
  phi = dxil_emit_phi(r->alu.module, dxil_module_get_int_type(r->alu.module, 32));
  if (!phi || !dxil_phi_add_incoming(phi, incoming, predecessors, 2)) return NULL;
  return dxil_instr_get_return_value(phi);
}
static XeDxilValue expand(Rov *r, XeDxilValue v) {
  return bin(r, DXIL_BINOP_OR, bin(r, DXIL_BINOP_SHL, bin(r, DXIL_BINOP_LSHR, v, ic(r, 1)), ic(r, 2)),
    bin(r, DXIL_BINOP_AND, v, ic(r, 1)));
}
static XeDxilValue depth_address(Rov *r, const XeDxilRovDepthOptions *o, XeDxilValue x, XeDxilValue y) {
  XeDxilValue gx, gy, sx, sy, u, v, half, tile_y, row, address;
  unsigned width = 80*o->scale_x, height = 16*o->scale_y;
  gx = bin(r, DXIL_BINOP_UDIV, x, ic(r, o->scale_x)); sx = bin(r, DXIL_BINOP_UREM, x, ic(r, o->scale_x));
  gy = bin(r, DXIL_BINOP_UDIV, y, ic(r, o->scale_y)); sy = bin(r, DXIL_BINOP_UREM, y, ic(r, o->scale_y));
  u = o->msaa == 2 ? expand(r, gx) : o->msaa == 1 ? bin(r, DXIL_BINOP_AND, gx, ic(r, ~2u)) : gx;
  v = o->msaa ? expand(r, gy) : gy;
  if (o->msaa == 1) v = bin(r, DXIL_BINOP_OR, v, bin(r, DXIL_BINOP_AND, gx, ic(r, 2)));
  u = bin(r, DXIL_BINOP_ADD, bin(r, DXIL_BINOP_MUL, u, ic(r, o->scale_x)), sx);
  v = bin(r, DXIL_BINOP_ADD, bin(r, DXIL_BINOP_MUL, v, ic(r, o->scale_y)), sy);
  half = bin(r, DXIL_BINOP_UDIV, u, ic(r, width/2)); tile_y = bin(r, DXIL_BINOP_UDIV, v, ic(r, height));
  row = bin(r, DXIL_BINOP_MUL, bin(r, DXIL_BINOP_UREM, v, ic(r, height)), ic(r, width));
  address = bin(r, DXIL_BINOP_ADD, word(r, o->depth_base_word), bin(r, DXIL_BINOP_MUL, tile_y, word(r, o->pitch_word)));
  address = bin(r, DXIL_BINOP_ADD, address, bin(r, DXIL_BINOP_MUL, bin(r, DXIL_BINOP_LSHR, half, ic(r, 1)), ic(r, width*height)));
  address = bin(r, DXIL_BINOP_ADD, address, bin(r, DXIL_BINOP_ADD, row, bin(r, DXIL_BINOP_UREM, u, ic(r, width/2))));
  address = bin(r, DXIL_BINOP_ADD, address, pick(r, flag(r, half, 1), ic(r, 0), ic(r, width/2)));
  return bin(r, DXIL_BINOP_UREM, address, ic(r, width*height*2048));
}
static int add_counter(Rov *r, XeDxilValue handle, XeDxilValue base, unsigned lane, XeDxilValue count, unsigned survey) {
  XeDxilValue args[7]; unsigned done;
  if (!conditional(r, cmp(r, DXIL_ICMP_NE, count, ic(r, 0)), &done)) return 0;
  args[0] = ic(r, 78); args[1] = handle; args[2] = ic(r, survey ? 8 : 0);
  args[3] = bin(r, DXIL_BINOP_ADD, base, ic(r, lane*4)); args[4] = ic(r, 0); args[5] = args[4];
  args[6] = survey ? ic(r, 1) : count;
  return call(r, "dx.op.atomicBinOp", DXIL_I32, args, 7) && end(r, done);
}
int xe_dxil_build_rov_depth(const XeDxilRovDepthOptions *o, XeDxilShaderBinary *binary) {
  static const float sample_x[4] = {-0.125f,0.375f,-0.375f,0.125f};
  static const float sample_y[4] = {-0.375f,-0.125f,0.125f,0.375f};
  Rov r;
  XeDxilDepthStencilState state;
  XeDxilDepthStencilResult tested;
  XeDxilValue args[5], position[3], front, flags, edram, counters, coverage, address;
  XeDxilValue dx, dy, abs_dx, abs_dy, slope, scale, offset, center, masks, ops, depth, packed_depth, previous, byte_address;
  XeDxilValue accum[3], counts[3], count, index, value, condition;
  const struct dxil_type *integer_type;
  unsigned i, j, enabled_done, sample_done, write_done, counter_done, host_sample, offset_dwords;
  int result = 0;
  if (!o || !binary || o->msaa > 2 || !o->scale_x || !o->scale_y || o->scale_x > 16 || o->scale_y > 16 ||
      o->system_constants_bytes < 4 || o->system_constants_bytes > 65536 || o->depth_function_shift > 29) return 0;
  i = o->system_constants_bytes/4;
  if (o->flags_word >= i || o->pitch_word >= i || o->depth_base_word >= i || o->counter_word >= i ||
      o->front_scale_word >= i || o->back_scale_word >= i || o->front_offset_word >= i || o->back_offset_word >= i ||
      o->front_masks_word >= i || o->back_masks_word >= i || o->front_ops_word >= i || o->back_ops_word >= i) return 0;
  memset(&r, 0, sizeof(r));
  r.shader = xe_dxil_shader_create(DXIL_PIXEL_SHADER); if (!r.shader) return 0;
  xe_dxil_alu_init(&r.alu, xe_dxil_shader_module(r.shader)); integer_type = dxil_module_get_int_type(r.alu.module, 32);
  if (xe_dxil_shader_signature(r.shader, 0, "SV_Position", 0, DXIL_SEM_POSITION, DXIL_PROG_SEM_POSITION,
        o->guest_linkage ? XE_DXIL_GUEST_POSITION_ROW : 0, 0, 4, DXIL_INTERP_LINEAR_NOPERSPECTIVE) != 0 ||
      xe_dxil_shader_signature_typed(r.shader, 0, "SV_IsFrontFace", 0, DXIL_SEM_IS_FRONT_FACE,
        DXIL_PROG_SEM_IS_FRONTFACE, o->guest_linkage ? XE_DXIL_GUEST_FRONT_FACE_ROW : 1, 0, 1, DXIL_INTERP_CONSTANT, DXIL_COMP_TYPE_U32) != 1 ||
      xe_dxil_shader_ordered_buffer(r.shader, 0, 2) != 0 ||
      xe_dxil_shader_resource(r.shader, XE_DXIL_RESOURCE_UAV, DXIL_RESOURCE_KIND_RAW_BUFFER, 0, 1, 1, 0) != 1 ||
      xe_dxil_shader_resource(r.shader, XE_DXIL_RESOURCE_CBV, DXIL_RESOURCE_KIND_CBUFFER, 1, 0, 1, o->system_constants_bytes) != 0) goto done;
  edram = xe_dxil_resource_handle(&r.alu, XE_DXIL_RESOURCE_UAV, 0, 2);
  counters = xe_dxil_resource_handle(&r.alu, XE_DXIL_RESOURCE_UAV, 1, 1);
  r.constants = xe_dxil_resource_handle(&r.alu, XE_DXIL_RESOURCE_CBV, 0, 0);
  args[0] = ic(&r, 4); args[1] = ic(&r, 0); args[2] = args[1]; args[4] = dxil_module_get_undef(r.alu.module, integer_type);
  for (i = 0; i < 3; ++i) { args[3] = dxil_module_get_int8_const(r.alu.module, (int8_t)i); position[i] = call(&r, "dx.op.loadInput", DXIL_F32, args, 5); }
  args[1] = ic(&r, 1); args[3] = dxil_module_get_int8_const(r.alu.module, 0);
  front = cmp(&r, DXIL_ICMP_NE, call(&r, "dx.op.loadInput", DXIL_I32, args, 5), ic(&r, 0));
  args[0] = ic(&r, 91); coverage = call(&r, "dx.op.coverage", DXIL_I32, args, 1);
  if (o->msaa == 1) coverage = bin(&r, DXIL_BINOP_OR, field(&r, coverage, 0, 1),
    bin(&r, DXIL_BINOP_SHL, field(&r, coverage, 3, 1), ic(&r, 1)));
  coverage = bin(&r, DXIL_BINOP_AND, coverage, ic(&r, (1u << (1u << o->msaa))-1));
  args[0] = ic(&r, 31); args[1] = coverage; count = call(&r, "dx.op.unaryBits", DXIL_I32, args, 2);
  for (i = 0; i < 3; ++i) {
    accum[i] = dxil_emit_alloca(r.alu.module, integer_type, ic(&r, 1), 4);
    if (!accum[i] || !dxil_emit_store(r.alu.module, i ? ic(&r, 0) : count, accum[i], 4, 0)) goto done;
  }
  flags = word(&r, o->flags_word);
  address = depth_address(&r, o, cast(&r, DXIL_CAST_FPTOUI, 0, position[0]), cast(&r, DXIL_CAST_FPTOUI, 0, position[1]));
  dx = unary(&r, 83, position[2]); dy = unary(&r, 84, position[2]);
  abs_dx = unary(&r, 6, dx); abs_dy = unary(&r, 6, dy);
  slope = pick(&r, cmp(&r, DXIL_FCMP_OGT, abs_dx, abs_dy), abs_dx, abs_dy);
  scale = cast(&r, DXIL_CAST_BITCAST, 1, pick(&r, front, word(&r, o->front_scale_word), word(&r, o->back_scale_word)));
  offset = cast(&r, DXIL_CAST_BITCAST, 1, pick(&r, front, word(&r, o->front_offset_word), word(&r, o->back_offset_word)));
  center = bin(&r, DXIL_BINOP_ADD, position[2], bin(&r, DXIL_BINOP_ADD, bin(&r, DXIL_BINOP_MUL, slope, scale), offset));
  masks = pick(&r, front, word(&r, o->front_masks_word), word(&r, o->back_masks_word));
  ops = pick(&r, front, word(&r, o->front_ops_word), word(&r, o->back_ops_word));
  state.depth_enabled = ic(&r, 1); state.depth_write = cast(&r, DXIL_CAST_ZEXT, 0, flag(&r, flags, o->depth_write_mask));
  state.depth_function = field(&r, flags, o->depth_function_shift, 7);
  state.stencil_enabled = cast(&r, DXIL_CAST_ZEXT, 0, flag(&r, flags, o->stencil_mask));
  state.stencil_function = field(&r, ops, 0, 7); state.reference = field(&r, masks, 0, 255);
  state.read_mask = field(&r, masks, 8, 255); state.write_mask = field(&r, masks, 16, 255);
  state.stencil_fail = field(&r, ops, 3, 7); state.depth_pass = field(&r, ops, 6, 7); state.depth_fail = field(&r, ops, 9, 7);
  if (r.alu.failed || !conditional(&r, flag(&r, flags, o->depth_stencil_mask), &enabled_done)) goto done;
  for (i = 0; i < (1u << o->msaa); ++i) {
    if (!conditional(&r, flag(&r, coverage, 1u << i), &sample_done)) goto done;
    host_sample = o->msaa == 1 && i == 1 ? 3 : i;
    depth = o->msaa ? bin(&r, DXIL_BINOP_ADD, center, bin(&r, DXIL_BINOP_ADD,
      bin(&r, DXIL_BINOP_MUL, dx, fc(&r, sample_x[host_sample])), bin(&r, DXIL_BINOP_MUL, dy, fc(&r, sample_y[host_sample])))) : center;
    depth = xe_dxil_alu_saturate(&r.alu, depth);
    packed_depth = quantize(&r, flag(&r, flags, o->float24_mask), depth);
    offset_dwords = 2*o->scale_x*(i&1) + 2*o->scale_y*(80*o->scale_x)*(i>>1);
    byte_address = bin(&r, DXIL_BINOP_MUL, bin(&r, DXIL_BINOP_ADD, address, ic(&r, offset_dwords)), ic(&r, 4));
    previous = xe_dxil_raw_buffer_load(&r.alu, edram, byte_address);
    if (!xe_dxil_edram_depth_stencil(&r.alu, &state, previous, packed_depth, &tested) ||
        !conditional(&r, tested.changed, &write_done) ||
        !xe_dxil_raw_buffer_store(&r.alu, edram, byte_address, tested.packed) || !end(&r, write_done)) goto done;
    value = bin(&r, DXIL_BINOP_XOR, tested.passed, dxil_module_get_int1_const(r.alu.module, 1));
    value = bin(&r, DXIL_BINOP_SUB, dxil_emit_load(r.alu.module, accum[0], 4, 0), cast(&r, DXIL_CAST_ZEXT, 0, value));
    if (!value || !dxil_emit_store(r.alu.module, value, accum[0], 4, 0)) goto done;
    if (o->full_counters && !o->survey) for (j = 1; j < 3; ++j) {
      value = cast(&r, DXIL_CAST_ZEXT, 0, j == 1 ? tested.depth_failed : tested.stencil_failed);
      value = bin(&r, DXIL_BINOP_ADD, dxil_emit_load(r.alu.module, accum[j], 4, 0), value);
      if (!value || !dxil_emit_store(r.alu.module, value, accum[j], 4, 0)) goto done;
    }
    if (!end(&r, sample_done)) goto done;
  }
  if (!end(&r, enabled_done)) goto done;
  index = word(&r, o->counter_word);
  condition = cmp(&r, DXIL_ICMP_NE, index, ic(&r, UINT32_MAX));
  if (!conditional(&r, condition, &counter_done)) goto done;
  index = bin(&r, DXIL_BINOP_MUL, index, ic(&r, 16));
  for (i = 0; i < 3; ++i) counts[i] = dxil_emit_load(r.alu.module, accum[i], 4, 0);
  if (!add_counter(&r, counters, index, 2, counts[0], o->survey)) goto done;
  if (o->full_counters && !o->survey) {
    if (!add_counter(&r, counters, index, 1, counts[1], 0) || !add_counter(&r, counters, index, 3, counts[2], 0)) goto done;
  }
  if (!end(&r, counter_done) || r.alu.failed || !dxil_emit_ret_void(r.alu.module)) goto done;
  result = xe_dxil_shader_finish(r.shader, binary);
done:
  xe_dxil_shader_destroy(r.shader); return result;
}
void xe_dxil_rov_depth_set_free(XeDxilShaderBinary binaries[3][2]) {
  unsigned msaa, survey;
  if (!binaries) return;
  for (msaa = 0; msaa < 3; ++msaa) for (survey = 0; survey < 2; ++survey)
    xe_dxil_shader_binary_free(&binaries[msaa][survey]);
}
int xe_dxil_build_rov_depth_set(const XeDxilRovDepthOptions *base, XeDxilShaderBinary binaries[3][2]) {
  XeDxilRovDepthOptions options;
  unsigned msaa, survey;
  if (!base || !binaries) return 0;
  for (msaa = 0; msaa < 3; ++msaa) for (survey = 0; survey < 2; ++survey)
    if (binaries[msaa][survey].data || binaries[msaa][survey].size) return 0;
  options = *base;
  for (msaa = 0; msaa < 3; ++msaa) for (survey = 0; survey < 2; ++survey) {
    options.msaa = msaa; options.survey = survey;
    if (!xe_dxil_build_rov_depth(&options, &binaries[msaa][survey])) {
      xe_dxil_rov_depth_set_free(binaries); return 0;
    }
  }
  return 1;
}
