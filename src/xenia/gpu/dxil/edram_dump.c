/* Direct DXIL EDRAM dump shader. MSVC C89; BSD license.
 * Addressing follows Xenia's EDRAM dump emitter, Ben Vanik and contributors. */
#include "edram_dump.h"
#include "edram_format.h"
#include "guest_resources.h"
#include "../../../../libretro/core_resolve_native_loads.h"
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
static int load_and_pack(XeDxilAlu *s, const XeDxilEdramDumpOptions *o, XeDxilValue texture,
                            XeDxilValue stencil, XeDxilValue x, XeDxilValue y,
                            XeDxilValue sample, XeDxilValue packed[2]) {
  XeDxilValue source[4], stencil_values[4], args[2], value;
  const struct dxil_func *function;
  int integer_source = !o->depth && o->source_uint;
  if (!texture_load(s, texture, integer_source, o->msaa, x, y, sample, source)) return 0;
  if (!o->depth) return xe_dxil_pack_edram_color(s, o->format, integer_source, source, packed) != 0;
  if (!texture_load(s, stencil, 1, o->msaa, x, y, sample, stencil_values)) return 0;
  if (o->format) {
    packed[0] = xe_dxil_pack_depth20e4(s, source[0], o->round_depth && !o->depth_converted_in_pixel_shader, 1);
  } else {
    value = bin(s, DXIL_BINOP_MUL, source[0], dxil_module_get_float_const(s->module, 16777215.0f));
    function = dxil_get_function(s->module, "dx.op.unary", DXIL_F32);
    args[0] = ic(s, 26); args[1] = value;
    value = function && value ? dxil_emit_call(s->module, function, args, 2) : NULL;
    packed[0] = value ? dxil_emit_cast(s->module, DXIL_CAST_FPTOUI, dxil_module_get_int_type(s->module, 32), value) : NULL;
  }
  packed[0] = bin(s, DXIL_BINOP_OR, bin(s, DXIL_BINOP_SHL, packed[0], ic(s, 8)), field(s, stencil_values[0], 0, 255));
  packed[1] = NULL;
  return packed[0] != NULL && !s->failed;
}
static XeDxilValue add(XeDxilAlu *s, XeDxilValue a, XeDxilValue b) { return bin(s, DXIL_BINOP_ADD, a, b); }
static XeDxilValue mul(XeDxilAlu *s, XeDxilValue a, XeDxilValue b) { return bin(s, DXIL_BINOP_MUL, a, b); }
static XeDxilValue shl(XeDxilAlu *s, XeDxilValue a, unsigned bits) { return bin(s, DXIL_BINOP_SHL, a, ic(s, bits)); }
static XeDxilValue shr(XeDxilAlu *s, XeDxilValue a, unsigned bits) { return bin(s, DXIL_BINOP_LSHR, a, ic(s, bits)); }
static XeDxilValue bor(XeDxilAlu *s, XeDxilValue a, XeDxilValue b) { return bin(s, DXIL_BINOP_OR, a, b); }
static XeDxilValue pick(XeDxilAlu *s, XeDxilValue condition, XeDxilValue yes, XeDxilValue no) {
  XeDxilValue value = condition && yes && no ? dxil_emit_select(s->module, condition, yes, no) : NULL;
  if (!value) s->failed = 1;
  return value;
}
static XeDxilValue equal(XeDxilAlu *s, XeDxilValue value, unsigned constant) {
  value = value ? dxil_emit_cmp(s->module, DXIL_ICMP_EQ, value, ic(s, constant)) : NULL;
  if (!value) s->failed = 1;
  return value;
}
static XeDxilValue combine_address(XeDxilAlu *s, XeDxilValue bytes, XeDxilValue bank,
                                     XeDxilValue pipe, XeDxilValue y_lsb) {
  return bor(s, bor(s, bor(s, shl(s, y_lsb, 4), shl(s, pipe, 6)), shl(s, bank, 11)),
    bor(s, bor(s, field(s, bytes, 0, 15), shl(s, field(s, bytes, 4, 1), 5)),
      bor(s, shl(s, field(s, bytes, 5, 7), 8), shl(s, shr(s, bytes, 8), 12))));
}
static XeDxilValue fill_bias(XeDxilAlu *s, XeDxilValue coordinate, unsigned extent, XeDxilValue enabled) {
  XeDxilValue limit = pick(s, enabled, ic(s, extent), ic(s, 0));
  return select_less(s, coordinate, limit, bin(s, DXIL_BINOP_SUB, limit, coordinate), ic(s, 0));
}
static int emit_direct_resolve(XeDxilAlu *s, const XeDxilEdramDumpOptions *o,
                                  XeDxilValue output, XeDxilValue texture, XeDxilValue stencil,
                                  XeDxilValue constants, XeDxilValue pitches, XeDxilValue offsets,
                                  XeDxilValue x, XeDxilValue y, unsigned words) {
  XeDxilValue info, coordinate, dest_info, dest_coordinate, dispatch, resolve_height;
  XeDxilValue tile_x, tile_y, in_x, in_y, dest_x, dest_y, condition, valid_x, valid_y;
  XeDxilValue tile, source_tile, source_pitch, source_x, source_y, sample, fill_enabled;
  XeDxilValue packed[8], pixel[2], pixel_x, endian, swap, swap8, swap10, swap64, swap_dwords;
  XeDxilValue a, b, value, bytes, bank, pipe, address2d, address3d, address, slice, pitch;
  XeDxilValue tiled_x, tiled_y, host_offset = NULL, gx, gy, hx, hy, group;
  XeDxilValue args[9], format, swap_bytes, swap_halves;
  const struct dxil_func *store;
  unsigned scale_x = o->native_layout ? 1 : o->scale_x, scale_y = o->native_layout ? 1 : o->scale_y;
  unsigned tile_width = ((words == 2 ? 40 : 80) * scale_x) >> (o->msaa >= 2);
  unsigned tile_height = (16 * scale_y) >> (o->msaa >= 1);
  unsigned pixels = 8 / words, pixels_per_store = 4 / words, bpp_log2 = words == 2 ? 3 : 2;
  unsigned scaled = !o->native_layout && (o->scale_x > 1 || o->scale_y > 1);
  unsigned i, j, blocks, group_x, group_y, second_offset, load_index;
  info = xe_dxil_cbuffer_word(s, constants, ic(s, 2));
  coordinate = xe_dxil_cbuffer_word(s, constants, ic(s, 3));
  dest_info = xe_dxil_cbuffer_word(s, constants, ic(s, 4));
  dest_coordinate = xe_dxil_cbuffer_word(s, constants, ic(s, 5));
  resolve_height = xe_dxil_cbuffer_word(s, constants, ic(s, 7));
  dispatch = xe_dxil_cbuffer_word(s, constants, ic(s, 8));
  x = mul(s, x, ic(s, pixels));
  tile_x = add(s, field(s, dispatch, 0, 1023), bin(s, DXIL_BINOP_UDIV, x, ic(s, tile_width)));
  tile_y = add(s, field(s, dispatch, 10, 1023), bin(s, DXIL_BINOP_UDIV, y, ic(s, tile_height)));
  in_x = bin(s, DXIL_BINOP_UREM, x, ic(s, tile_width)); in_y = bin(s, DXIL_BINOP_UREM, y, ic(s, tile_height));
  dest_x = bin(s, DXIL_BINOP_SUB, add(s, mul(s, tile_x, ic(s, tile_width)), in_x),
    mul(s, shl(s, field(s, coordinate, 0, 15), 3), ic(s, scale_x)));
  dest_y = bin(s, DXIL_BINOP_SUB, add(s, mul(s, tile_y, ic(s, tile_height)), in_y),
    mul(s, shl(s, field(s, coordinate, 4, 1), 3), ic(s, scale_y)));
  valid_x = dxil_emit_cmp(s->module, DXIL_ICMP_ULT, dest_x, mul(s, shl(s, field(s, coordinate, 5, 2047), 3), ic(s, scale_x)));
  valid_y = dxil_emit_cmp(s->module, DXIL_ICMP_ULT, dest_y, mul(s, shl(s, resolve_height, 3), ic(s, scale_y)));
  condition = bin(s, DXIL_BINOP_AND, valid_x, valid_y);
  fill_enabled = equal(s, field(s, info, 29, 1), 1);
  tile = add(s, add(s, field(s, info, 13, 2047), mul(s, field(s, info, 0, 1023), tile_y)), tile_x);
  source_tile = bin(s, DXIL_BINOP_SUB, tile, field(s, offsets, 12, 2047));
  source_pitch = field(s, pitches, 10, 1023);
  source_x = add(s, mul(s, bin(s, DXIL_BINOP_UREM, source_tile, source_pitch), ic(s, tile_width)), in_x);
  source_y = add(s, mul(s, bin(s, DXIL_BINOP_UDIV, source_tile, source_pitch), ic(s, tile_height)), in_y);
  if (scaled && scale_y > 1) source_y = add(s, source_y, fill_bias(s, dest_y, scale_y / 2, fill_enabled));
  if (o->native_source && scaled) source_y = bin(s, DXIL_BINOP_UDIV, source_y, ic(s, o->scale_y));
  sample = field(s, dest_coordinate, 28, 7);
  if (o->msaa == 1) sample = pick(s, equal(s, field(s, sample, 0, 1), 1),
    ic(s, o->msaa_2x_supported ? 0 : 3), ic(s, o->msaa_2x_supported ? 1 : 0));
  else if (o->msaa == 2) sample = field(s, sample, 0, 3);
  if (s->failed || !condition || !dxil_function_reserve_blocks(s->module, 2, &blocks) ||
      !dxil_emit_branch(s->module, condition, blocks, blocks + 1)) return 0;
  s->module->cur_emitting_func->basic_block_ids[blocks] = (int)s->module->cur_emitting_func->curr_block;
  for (i = 0; i < pixels; ++i) {
    load_index = xe_resolve_native_load_index(pixels, o->scale_x, o->native_source && scaled, i);
    if (load_index == i) {
      pixel_x = add(s, source_x, ic(s, i));
      if (scaled && i < scale_x / 2) pixel_x = add(s, pixel_x,
        fill_bias(s, add(s, dest_x, ic(s, i)), scale_x / 2, fill_enabled));
      if (o->native_source && scaled) pixel_x = bin(s, DXIL_BINOP_UDIV, pixel_x, ic(s, o->scale_x));
      if (!load_and_pack(s, o, texture, stencil, pixel_x, source_y, sample, pixel)) return 0;
    }
    for (j = 0; j < words; ++j) packed[i * words + j] = load_index == i ? pixel[j] : packed[load_index * words + j];
  }
  swap = equal(s, field(s, dest_info, 24, 1), 1); format = field(s, info, 24, 15);
  swap8 = bor(s, equal(s, format, 0), equal(s, format, 1));
  swap10 = bor(s, bor(s, equal(s, format, 2), equal(s, format, 3)), bor(s, equal(s, format, 10), equal(s, format, 12)));
  swap64 = bin(s, DXIL_BINOP_AND, swap, bor(s, equal(s, format, 5), equal(s, format, 7)));
  endian = field(s, dest_info, 0, 7); swap_dwords = equal(s, endian, 4);
  if (words == 2) endian = pick(s, swap_dwords, ic(s, 2), endian);
  swap_bytes = bor(s, equal(s, endian, 1), equal(s, endian, 2));
  swap_halves = bor(s, equal(s, endian, 2), equal(s, endian, 3));
  for (i = 0; i < pixels; ++i) {
    value = packed[i * words];
    if (words == 1) {
      a = bor(s, bor(s, bin(s, DXIL_BINOP_AND, value, ic(s, ~UINT32_C(0xff00ff))),
        shl(s, field(s, value, 0, 255), 16)), field(s, value, 16, 255));
      b = bor(s, bor(s, bin(s, DXIL_BINOP_AND, value, ic(s, ~UINT32_C(0x3ff003ff))),
        shl(s, field(s, value, 0, 1023), 20)), field(s, value, 20, 1023));
      packed[i] = pick(s, swap, pick(s, swap8, a, pick(s, swap10, b, value)), value);
    } else {
      a = packed[i * 2]; b = packed[i * 2 + 1];
      value = pick(s, swap64, bor(s, bin(s, DXIL_BINOP_AND, a, ic(s, 0xffff0000)), field(s, b, 0, 65535)), a);
      b = pick(s, swap64, bor(s, bin(s, DXIL_BINOP_AND, b, ic(s, 0xffff0000)), field(s, a, 0, 65535)), b);
      packed[i * 2] = pick(s, swap_dwords, b, value); packed[i * 2 + 1] = pick(s, swap_dwords, value, b);
    }
    for (j = 0; j < words; ++j) {
      value = packed[i * words + j];
      value = pick(s, swap_bytes, bor(s, shl(s, field(s, value, 0, 0x00ff00ff), 8),
        shr(s, bin(s, DXIL_BINOP_AND, value, ic(s, 0xff00ff00)), 8)), value);
      packed[i * words + j] = pick(s, swap_halves, bor(s, shl(s, value, 16), shr(s, value, 16)), value);
    }
  }
  tiled_x = add(s, dest_x, mul(s, shl(s, field(s, dest_coordinate, 20, 15), 3), ic(s, scale_x)));
  tiled_y = add(s, dest_y, mul(s, shl(s, field(s, dest_coordinate, 24, 15), 3), ic(s, scale_y)));
  if (scaled) {
    group_x = bpp_log2 >= 3 ? 5 - bpp_log2 : 4; group_y = 3 - (bpp_log2 < 2 ? bpp_log2 : 2);
    hx = shr(s, tiled_x, group_x); hy = shr(s, tiled_y, group_y);
    gx = bin(s, DXIL_BINOP_UDIV, hx, ic(s, scale_x)); gy = bin(s, DXIL_BINOP_UDIV, hy, ic(s, scale_y));
    group = add(s, mul(s, bin(s, DXIL_BINOP_SUB, hx, mul(s, gx, ic(s, scale_x))), ic(s, scale_y)),
      bin(s, DXIL_BINOP_SUB, hy, mul(s, gy, ic(s, scale_y))));
    host_offset = bor(s, bor(s, shl(s, group, group_x + bpp_log2 + group_y),
      shl(s, field(s, tiled_y, 0, (1u << group_y) - 1), group_x + bpp_log2)),
      shl(s, field(s, tiled_x, 0, (1u << group_x) - 1), bpp_log2));
    tiled_x = shl(s, gx, group_x); tiled_y = shl(s, gy, group_y);
  }
  pitch = field(s, dest_coordinate, 0, 1023);
  bytes = shl(s, bor(s, shl(s, add(s, mul(s, shr(s, tiled_y, 5), pitch), shr(s, tiled_x, 5)), 6),
    bor(s, shl(s, field(s, tiled_y, 1, 7), 3), field(s, tiled_x, 0, 7))), bpp_log2);
  bank = field(s, tiled_y, 4, 1);
  pipe = bin(s, DXIL_BINOP_XOR, field(s, tiled_x, 3, 3), shl(s, field(s, tiled_y, 3, 1), 1));
  address2d = combine_address(s, bytes, bank, pipe, field(s, tiled_y, 0, 1));
  slice = field(s, dest_info, 4, 7);
  a = shl(s, field(s, dest_coordinate, 10, 1023), 1);
  bytes = shl(s, bor(s, shl(s, add(s, mul(s, add(s, mul(s, shr(s, slice, 2), a), shr(s, tiled_y, 4)), pitch),
    shr(s, tiled_x, 5)), 7), bor(s, bor(s, shl(s, field(s, slice, 0, 3), 5), shl(s, field(s, tiled_y, 1, 3), 3)),
    field(s, tiled_x, 0, 7))), bpp_log2);
  bank = field(s, bin(s, DXIL_BINOP_XOR, shr(s, tiled_y, 3), shr(s, slice, 2)), 0, 1);
  pipe = bin(s, DXIL_BINOP_XOR, field(s, tiled_x, 3, 3), shl(s, bank, 1));
  address3d = combine_address(s, bytes, bank, pipe, field(s, tiled_y, 0, 1));
  address = pick(s, equal(s, field(s, dest_info, 3, 1), 1), address3d, address2d);
  if (scaled) address = add(s, mul(s, address, ic(s, scale_x * scale_y)), host_offset);
  else address = add(s, address, xe_dxil_cbuffer_word(s, constants, ic(s, 6)));
  /* One run has two 16-byte stores, preserving the direct resolve's
   * contiguous writes without an EDRAM round trip. */
  second_offset = scaled ? pixels_per_store << bpp_log2 :
    ((pixels_per_store & 7) << bpp_log2 & 15) |
    ((((pixels_per_store & 7) << bpp_log2 >> 4) & 1) << 5) |
    ((((pixels_per_store & 7) << bpp_log2 >> 5) & 7) << 8) |
    (((pixels_per_store >> 3) & 3) << 6);
  store = dxil_get_function(s->module, "dx.op.bufferStore", DXIL_I32);
  args[0] = ic(s, 69); args[1] = output; args[3] = dxil_module_get_undef(s->module, dxil_module_get_int_type(s->module, 32));
  args[8] = dxil_module_get_int8_const(s->module, 15);
  for (i = 0; i < 2; ++i) {
    args[2] = bin(s, DXIL_BINOP_AND, add(s, address, ic(s, i ? second_offset : 0)), ic(s, ~15u));
    for (j = 0; j < 4; ++j) args[4 + j] = packed[i * 4 + j];
    if (s->failed || !store || !args[2] || !dxil_emit_call_void(s->module, store, args, 9)) return 0;
  }
  if (!dxil_emit_branch(s->module, NULL, blocks + 1, 0)) return 0;
  s->module->cur_emitting_func->basic_block_ids[blocks + 1] = (int)s->module->cur_emitting_func->curr_block;
  return !s->failed;
}
int xe_dxil_build_edram_dump(const XeDxilEdramDumpOptions *o, XeDxilShaderBinary *binary_out) {
  XeDxilShaderModule *shader;
  XeDxilAlu s;
  XeDxilValue output, texture, stencil = NULL, constants, pitches, offsets;
  XeDxilValue x, y, tile_x, tile_y, local_x, local_y, tile, address;
  XeDxilValue source_tile, source_pitch, source_x, source_y, sample, u, v, sub_x, sub_y;
  XeDxilValue packed[2], args[9];
  const struct dxil_func *function;
  unsigned words, layout_x, layout_y, width, height, kind, i;
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
  if (!xe_dxil_shader_threads(shader, 8, o->direct_resolve ? 8 : 16, 1) ||
      xe_dxil_shader_resource(shader, XE_DXIL_RESOURCE_UAV, DXIL_RESOURCE_KIND_RAW_BUFFER, o->destination_space, 0, 1, 0) != 0 ||
      xe_dxil_shader_resource_typed(shader, XE_DXIL_RESOURCE_SRV, kind, o->source_space, 0, 1, 0,
        integer_source ? DXIL_COMP_TYPE_U32 : DXIL_COMP_TYPE_F32) != 0 ||
      (o->depth && xe_dxil_shader_resource_typed(shader, XE_DXIL_RESOURCE_SRV, kind, o->source_space, 1, 1, 0, DXIL_COMP_TYPE_U32) != 1) ||
      xe_dxil_shader_resource(shader, XE_DXIL_RESOURCE_CBV, DXIL_RESOURCE_KIND_CBUFFER, 31, 1, 1, o->direct_resolve ? 48 : 16) != 0) goto done;
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
  if (o->direct_resolve) {
    if (emit_direct_resolve(&s, o, output, texture, stencil, constants, pitches, offsets, x, y, words) &&
        dxil_emit_ret_void(s.module)) result = xe_dxil_shader_finish(shader, binary_out);
    goto done;
  }
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
  if (!load_and_pack(&s, o, texture, stencil, source_x, source_y, sample, packed)) goto done;
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
