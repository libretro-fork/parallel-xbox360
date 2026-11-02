/* Direct DXIL EDRAM dump shader. MSVC C89; BSD license.
 * Addressing follows Xenia's EDRAM dump emitter, Ben Vanik and contributors. */
#include "edram_dump.h"
#include "edram_format.h"
#include "guest_resources.h"
#include <dxil_module.h>
#include <dxil_function.h>

static XeDxilValue ic(XeDxilAlu *s, uint32_t value) { return dxil_module_get_int32_const(s->module, (int32_t)value); }
static XeDxilValue bin(XeDxilAlu *s, enum dxil_bin_opcode op, XeDxilValue a, XeDxilValue b) {
  XeDxilValue value = a && b ? dxil_emit_binop(s->module, op, a, b, 0) : NULL;
  if (!value) s->failed = 1;
  return value;
}
static XeDxilValue field(XeDxilAlu *s, XeDxilValue value, unsigned shift, uint32_t mask) {
  return bin(s, DXIL_BINOP_AND, shift ? bin(s, DXIL_BINOP_LSHR, value, ic(s, shift)) : value, ic(s, mask));
}
static XeDxilValue select_less(XeDxilAlu *s, XeDxilValue a, XeDxilValue b, XeDxilValue yes, XeDxilValue no) {
  XeDxilValue condition = a && b ? dxil_emit_cmp(s->module, DXIL_ICMP_ULT, a, b) : NULL;
  XeDxilValue value = condition && yes && no ? dxil_emit_select(s->module, condition, yes, no) : NULL;
  if (!value) s->failed = 1;
  return value;
}
static int texture_load(XeDxilAlu *s, XeDxilValue handle, int integer_view, unsigned msaa,
                           XeDxilValue x, XeDxilValue y, XeDxilValue sample, XeDxilValue values[4]) {
  XeDxilValue args[9], loaded;
  const struct dxil_func *function = dxil_get_function(s->module, "dx.op.textureLoad", integer_view ? DXIL_I32 : DXIL_F32);
  unsigned i;
  args[0] = ic(s, 66); args[1] = handle; args[2] = msaa ? sample : ic(s, 0);
  args[3] = x; args[4] = y; args[5] = dxil_module_get_undef(s->module, dxil_module_get_int_type(s->module, 32));
  args[6] = ic(s, 0); args[7] = args[6]; args[8] = args[5];
  for (i = 0; i < 9; ++i) if (!args[i]) return 0;
  loaded = function ? dxil_emit_call(s->module, function, args, 9) : NULL;
  if (!loaded) return 0;
  for (i = 0; i < 4; ++i) { values[i] = dxil_emit_extractval(s->module, loaded, i); if (!values[i]) return 0; }
  return 1;
}
int xe_dxil_build_edram_dump(const XeDxilEdramDumpOptions *o, XeDxilShaderBinary *binary_out) {
  XeDxilShaderModule *shader;
  XeDxilAlu s;
  XeDxilValue output, texture, stencil = NULL, constants, pitches, offsets;
  XeDxilValue x, y, tile_x, tile_y, local_x, local_y, tile, address;
  XeDxilValue source_tile, source_pitch, source_x, source_y, sample, u, v, sub_x, sub_y;
  XeDxilValue source[4], stencil_values[4], packed[2], args[9], value;
  const struct dxil_func *function;
  unsigned words, layout_x, layout_y, width, height, kind, i, depth_round;
  int result = 0, integer_source;
  if (!o || !binary_out || o->msaa > 2 || !o->scale_x || !o->scale_y || o->scale_x > 16 || o->scale_y > 16 ||
      (o->depth && o->format > 1) || (!o->depth && o->format != 0 && o->format != 1 && o->format != 2 &&
       o->format != 3 && o->format != 4 && o->format != 5 && o->format != 6 && o->format != 7 &&
       o->format != 10 && o->format != 12 && o->format != 14 && o->format != 15)) return 0;
  words = !o->depth && (o->format == 5 || o->format == 7 || o->format == 15) ? 2 : 1;
  layout_x = o->native_layout ? 1 : o->scale_x; layout_y = o->native_layout ? 1 : o->scale_y;
  width = (words == 2 ? 40 : 80) * layout_x; height = 16 * layout_y;
  integer_source = !o->depth && o->source_uint;
  shader = xe_dxil_shader_create(DXIL_COMPUTE_SHADER);
  if (!shader) return 0;
  xe_dxil_alu_init(&s, xe_dxil_shader_module(shader));
  kind = o->msaa ? DXIL_RESOURCE_KIND_TEXTURE2DMS : DXIL_RESOURCE_KIND_TEXTURE2D;
  if (!xe_dxil_shader_threads(shader, 8, 16, 1) ||
      xe_dxil_shader_resource(shader, XE_DXIL_RESOURCE_UAV, DXIL_RESOURCE_KIND_RAW_BUFFER, o->destination_space, 0, 1, 0) != 0 ||
      xe_dxil_shader_resource_typed(shader, XE_DXIL_RESOURCE_SRV, kind, o->source_space, 0, 1, 0,
        integer_source ? DXIL_COMP_TYPE_U32 : DXIL_COMP_TYPE_F32) != 0 ||
      (o->depth && xe_dxil_shader_resource_typed(shader, XE_DXIL_RESOURCE_SRV, kind, o->source_space, 1, 1, 0, DXIL_COMP_TYPE_U32) != 1) ||
      xe_dxil_shader_resource(shader, XE_DXIL_RESOURCE_CBV, DXIL_RESOURCE_KIND_CBUFFER, 31, 1, 1, 16) != 0) goto done;
  output = xe_dxil_resource_handle(&s, XE_DXIL_RESOURCE_UAV, 0, 0);
  texture = xe_dxil_resource_handle(&s, XE_DXIL_RESOURCE_SRV, 0, 0);
  if (o->depth) stencil = xe_dxil_resource_handle(&s, XE_DXIL_RESOURCE_SRV, 1, 1);
  constants = xe_dxil_resource_handle(&s, XE_DXIL_RESOURCE_CBV, 0, 1);
  pitches = xe_dxil_cbuffer_word(&s, constants, ic(&s, 0));
  offsets = xe_dxil_cbuffer_word(&s, constants, ic(&s, 1));
  function = dxil_get_function(s.module, "dx.op.threadId", DXIL_I32);
  args[0] = ic(&s, 93); args[1] = ic(&s, 0);
  x = function ? dxil_emit_call(s.module, function, args, 2) : NULL;
  args[1] = ic(&s, 1); y = function ? dxil_emit_call(s.module, function, args, 2) : NULL;
  if (!output || !texture || !constants || !x || !y || (o->depth && !stencil)) goto done;
  tile_x = bin(&s, DXIL_BINOP_UDIV, x, ic(&s, width)); local_x = bin(&s, DXIL_BINOP_UREM, x, ic(&s, width));
  tile_y = bin(&s, DXIL_BINOP_UDIV, y, ic(&s, height)); local_y = bin(&s, DXIL_BINOP_UREM, y, ic(&s, height));
  tile = bin(&s, DXIL_BINOP_ADD, field(&s, offsets, 0, 4095),
    bin(&s, DXIL_BINOP_ADD, bin(&s, DXIL_BINOP_MUL, tile_y, field(&s, pitches, 0, 1023)), tile_x));
  address = bin(&s, DXIL_BINOP_ADD, bin(&s, DXIL_BINOP_MUL, field(&s, tile, 0, 2047), ic(&s, width * height)),
    bin(&s, DXIL_BINOP_ADD, bin(&s, DXIL_BINOP_MUL, local_y, ic(&s, width)), local_x));
  if (o->depth) address = bin(&s, DXIL_BINOP_ADD, address,
    select_less(&s, local_x, ic(&s, width / 2), ic(&s, width / 2), ic(&s, 0u - width / 2)));
  source_tile = bin(&s, DXIL_BINOP_SUB, tile, field(&s, offsets, 12, 2047));
  source_pitch = field(&s, pitches, 10, 1023);
  source_x = bin(&s, DXIL_BINOP_ADD, bin(&s, DXIL_BINOP_MUL,
    bin(&s, DXIL_BINOP_UREM, source_tile, source_pitch), ic(&s, width)), local_x);
  source_y = bin(&s, DXIL_BINOP_ADD, bin(&s, DXIL_BINOP_MUL,
    bin(&s, DXIL_BINOP_UDIV, source_tile, source_pitch), ic(&s, height)), local_y);
  sample = ic(&s, 0);
  if (o->msaa) {
    u = bin(&s, DXIL_BINOP_UDIV, source_x, ic(&s, layout_x)); sub_x = bin(&s, DXIL_BINOP_UREM, source_x, ic(&s, layout_x));
    v = bin(&s, DXIL_BINOP_UDIV, source_y, ic(&s, layout_y)); sub_y = bin(&s, DXIL_BINOP_UREM, source_y, ic(&s, layout_y));
    if (o->msaa == 2) {
      sample = bin(&s, DXIL_BINOP_OR, field(&s, u, 1, 1), bin(&s, DXIL_BINOP_SHL, field(&s, v, 1, 1), ic(&s, 1)));
      source_x = bin(&s, DXIL_BINOP_OR, bin(&s, DXIL_BINOP_SHL, bin(&s, DXIL_BINOP_LSHR, u, ic(&s, 2)), ic(&s, 1)), field(&s, u, 0, 1));
      source_y = bin(&s, DXIL_BINOP_OR, bin(&s, DXIL_BINOP_SHL, bin(&s, DXIL_BINOP_LSHR, v, ic(&s, 2)), ic(&s, 1)), field(&s, v, 0, 1));
    } else {
      /* D3D10's guest 2x order is {1,0}, or {0,3} on a 4x attachment. */
      sample = select_less(&s, field(&s, u, 1, 1), ic(&s, 1), ic(&s, o->msaa_2x_supported ? 1 : 0),
        ic(&s, o->msaa_2x_supported ? 0 : 3));
      source_x = bin(&s, DXIL_BINOP_OR, bin(&s, DXIL_BINOP_AND, u, ic(&s, ~2u)), field(&s, v, 0, 2));
      source_y = bin(&s, DXIL_BINOP_OR, bin(&s, DXIL_BINOP_LSHR,
        bin(&s, DXIL_BINOP_AND, v, ic(&s, ~3u)), ic(&s, 1)), field(&s, v, 0, 1));
    }
    if (!o->native_source) {
      source_x = bin(&s, DXIL_BINOP_ADD, bin(&s, DXIL_BINOP_MUL, source_x, ic(&s, layout_x)), sub_x);
      source_y = bin(&s, DXIL_BINOP_ADD, bin(&s, DXIL_BINOP_MUL, source_y, ic(&s, layout_y)), sub_y);
    }
  } else if (o->native_source && !o->native_layout) {
    source_x = bin(&s, DXIL_BINOP_UDIV, source_x, ic(&s, o->scale_x));
    source_y = bin(&s, DXIL_BINOP_UDIV, source_y, ic(&s, o->scale_y));
  }
  if (!texture_load(&s, texture, integer_source, o->msaa, source_x, source_y, sample, source)) goto done;
  if (o->depth) {
    if (!texture_load(&s, stencil, 1, o->msaa, source_x, source_y, sample, stencil_values)) goto done;
    if (o->format) {
      depth_round = o->round_depth && !o->depth_converted_in_pixel_shader;
      packed[0] = xe_dxil_pack_depth20e4(&s, source[0], (int)depth_round, 1);
    } else {
      value = bin(&s, DXIL_BINOP_MUL, source[0], dxil_module_get_float_const(s.module, 16777215.0f));
      function = dxil_get_function(s.module, "dx.op.unary", DXIL_F32);
      args[0] = ic(&s, 26); args[1] = value;
      value = function && value ? dxil_emit_call(s.module, function, args, 2) : NULL;
      packed[0] = value ? dxil_emit_cast(s.module, DXIL_CAST_FPTOUI, dxil_module_get_int_type(s.module, 32), value) : NULL;
    }
    packed[0] = bin(&s, DXIL_BINOP_OR, bin(&s, DXIL_BINOP_SHL, packed[0], ic(&s, 8)), field(&s, stencil_values[0], 0, 255));
    packed[1] = NULL;
  } else if (xe_dxil_pack_edram_color(&s, o->format, integer_source, source, packed) != (int)words) goto done;
  if (s.failed || !packed[0]) goto done;
  address = bin(&s, DXIL_BINOP_MUL, address, ic(&s, words * 4));
  function = dxil_get_function(s.module, "dx.op.bufferStore", DXIL_I32);
  args[0] = ic(&s, 69); args[1] = output; args[3] = dxil_module_get_undef(s.module, dxil_module_get_int_type(s.module, 32));
  args[5] = args[3]; args[6] = args[3]; args[7] = args[3]; args[8] = dxil_module_get_int8_const(s.module, 1);
  for (i = 0; i < words; ++i) {
    args[2] = bin(&s, DXIL_BINOP_ADD, address, ic(&s, i * 4)); args[4] = packed[i];
    if (!function || !args[2] || !packed[i] || !dxil_emit_call_void(s.module, function, args, 9)) goto done;
  }
  if (!s.failed && dxil_emit_ret_void(s.module)) result = xe_dxil_shader_finish(shader, binary_out);
done:
  xe_dxil_shader_destroy(shader);
  return result;
}
