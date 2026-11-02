/* Native DXIL ownership transfers. MSVC C89; BSD license.
 * Guest layout rules follow Xenia's transfer emitter, Ben Vanik and contributors. */
#include "edram_transfer.h"
#include "edram_format.h"
#include "guest_resources.h"
#include <dxil_module.h>
#include <dxil_function.h>

typedef struct Transfer {
  XeDxilAlu alu;
  XeDxilShaderModule *shader;
  const XeDxilEdramTransferOptions *options;
  unsigned source_color, destination_color, stencil_bit, host_depth, host_copy;
  unsigned source_words, destination_words, source_x_scale, source_y_scale;
  unsigned destination_x_scale, destination_y_scale;
  XeDxilValue source, stencil, host, constants;
} Transfer;
typedef struct Canonical { XeDxilValue u, v, sub_x, sub_y; } Canonical;

static XeDxilValue ic(Transfer *t, uint32_t value) { return dxil_module_get_int32_const(t->alu.module, (int32_t)value); }
static XeDxilValue fc(Transfer *t, float value) { return dxil_module_get_float_const(t->alu.module, value); }
static XeDxilValue bin(Transfer *t, enum dxil_bin_opcode op, XeDxilValue a, XeDxilValue b) {
  XeDxilValue value = a && b ? dxil_emit_binop(t->alu.module, op, a, b, 0) : NULL;
  if (!value) t->alu.failed = 1;
  return value;
}
static XeDxilValue add(Transfer *t, XeDxilValue a, XeDxilValue b) { return bin(t, DXIL_BINOP_ADD, a, b); }
static XeDxilValue mul(Transfer *t, XeDxilValue a, XeDxilValue b) { return bin(t, DXIL_BINOP_MUL, a, b); }
static XeDxilValue shl(Transfer *t, XeDxilValue a, unsigned bits) { return bin(t, DXIL_BINOP_SHL, a, ic(t, bits)); }
static XeDxilValue shr(Transfer *t, XeDxilValue a, unsigned bits) { return bin(t, DXIL_BINOP_LSHR, a, ic(t, bits)); }
static XeDxilValue field(Transfer *t, XeDxilValue a, unsigned bits, unsigned mask) { return bin(t, DXIL_BINOP_AND, shr(t, a, bits), ic(t, mask)); }
static XeDxilValue bor(Transfer *t, XeDxilValue a, XeDxilValue b) { return bin(t, DXIL_BINOP_OR, a, b); }
static XeDxilValue compare(Transfer *t, enum dxil_cmp_pred op, XeDxilValue a, XeDxilValue b) {
  XeDxilValue value = a && b ? dxil_emit_cmp(t->alu.module, op, a, b) : NULL;
  if (!value) t->alu.failed = 1;
  return value;
}
static XeDxilValue pick(Transfer *t, XeDxilValue condition, XeDxilValue a, XeDxilValue b) {
  XeDxilValue value = condition && a && b ? dxil_emit_select(t->alu.module, condition, a, b) : NULL;
  if (!value) t->alu.failed = 1;
  return value;
}
static XeDxilValue cast(Transfer *t, enum dxil_cast_opcode op, int floating, XeDxilValue value) {
  value = value ? dxil_emit_cast(t->alu.module, op, floating ? dxil_module_get_float_type(t->alu.module, 32) :
    dxil_module_get_int_type(t->alu.module, 32), value) : NULL;
  if (!value) t->alu.failed = 1;
  return value;
}
static unsigned color_words(unsigned format) { return format == 5 || format == 7 || format == 15 ? 2 : 1; }
static unsigned components(unsigned format) { return format == 14 ? 1 : format == 4 || format == 6 || format == 15 ? 2 : 4; }
static unsigned storage_format(unsigned format) { return format == 10 ? 2 : format == 12 ? 3 : format; }
static int color_format_valid(unsigned format) {
  return format <= 7 || format == 10 || format == 12 || format == 14 || format == 15;
}
static Canonical canonicalize(Transfer *t, unsigned msaa, XeDxilValue x, XeDxilValue y,
                                  XeDxilValue sample, unsigned sx, unsigned sy) {
  Canonical c;
  XeDxilValue guest_x = bin(t, DXIL_BINOP_UDIV, x, ic(t, sx));
  XeDxilValue guest_y = bin(t, DXIL_BINOP_UDIV, y, ic(t, sy));
  XeDxilValue guest_sample;
  c.sub_x = bin(t, DXIL_BINOP_UREM, x, ic(t, sx)); c.sub_y = bin(t, DXIL_BINOP_UREM, y, ic(t, sy));
  if (msaa == 2) {
    c.u = bor(t, bor(t, shl(t, shr(t, guest_x, 1), 2), shl(t, field(t, sample, 0, 1), 1)), field(t, guest_x, 0, 1));
    c.v = bor(t, bor(t, shl(t, shr(t, guest_y, 1), 2), field(t, sample, 0, 2)), field(t, guest_y, 0, 1));
  } else if (msaa == 1) {
    guest_sample = t->options->msaa_2x_supported ? bin(t, DXIL_BINOP_XOR, sample, ic(t, 1)) : shr(t, sample, 1);
    c.u = bor(t, bin(t, DXIL_BINOP_AND, guest_x, ic(t, ~2u)), shl(t, field(t, guest_sample, 0, 1), 1));
    c.v = bor(t, bor(t, shl(t, shr(t, guest_y, 1), 2), field(t, guest_x, 0, 2)), field(t, guest_y, 0, 1));
  } else { c.u = guest_x; c.v = guest_y; }
  return c;
}
static void decanonicalize(Transfer *t, unsigned msaa, const Canonical *c, unsigned sx, unsigned sy,
                             XeDxilValue *x, XeDxilValue *y, XeDxilValue *sample) {
  XeDxilValue gx, gy, guest_sample;
  *sample = ic(t, 0);
  if (msaa == 2) {
    gx = bor(t, shl(t, shr(t, c->u, 2), 1), field(t, c->u, 0, 1));
    gy = bor(t, shl(t, shr(t, c->v, 2), 1), field(t, c->v, 0, 1));
    *sample = bor(t, field(t, c->u, 1, 1), field(t, c->v, 0, 2));
  } else if (msaa == 1) {
    gx = bor(t, bin(t, DXIL_BINOP_AND, c->u, ic(t, ~2u)), field(t, c->v, 0, 2));
    gy = bor(t, shl(t, shr(t, c->v, 2), 1), field(t, c->v, 0, 1));
    guest_sample = field(t, c->u, 1, 1);
    *sample = t->options->msaa_2x_supported ? bin(t, DXIL_BINOP_XOR, guest_sample, ic(t, 1)) : mul(t, guest_sample, ic(t, 3));
  } else { gx = c->u; gy = c->v; }
  *x = add(t, mul(t, gx, ic(t, sx)), c->sub_x); *y = add(t, mul(t, gy, ic(t, sy)), c->sub_y);
}
static int load_texture(Transfer *t, XeDxilValue handle, unsigned msaa, int integer_view,
                           XeDxilValue x, XeDxilValue y, XeDxilValue sample, XeDxilValue values[4]) {
  XeDxilValue args[9], value;
  const struct dxil_func *function = dxil_get_function(t->alu.module, "dx.op.textureLoad", integer_view ? DXIL_I32 : DXIL_F32);
  unsigned i;
  args[0] = ic(t, 66); args[1] = handle; args[2] = msaa ? sample : ic(t, 0); args[3] = x; args[4] = y;
  args[5] = dxil_module_get_undef(t->alu.module, dxil_module_get_int_type(t->alu.module, 32));
  args[6] = ic(t, 0); args[7] = args[6]; args[8] = args[5];
  for (i = 0; i < 9; ++i) if (!args[i]) return 0;
  value = function ? dxil_emit_call(t->alu.module, function, args, 9) : NULL;
  if (!value) return 0;
  for (i = 0; i < 4; ++i) { values[i] = dxil_emit_extractval(t->alu.module, value, i); if (!values[i]) return 0; }
  return 1;
}
static XeDxilValue pack_depth(Transfer *t, unsigned format, XeDxilValue depth) {
  XeDxilValue args[2], value;
  const struct dxil_func *function;
  if (format) return xe_dxil_pack_depth20e4(&t->alu, depth,
    t->options->round_depth && !t->options->depth_converted_in_pixel_shader, 1);
  args[0] = ic(t, 26); args[1] = mul(t, depth, fc(t, 16777215.0f));
  function = dxil_get_function(t->alu.module, "dx.op.unary", DXIL_F32);
  value = function && args[1] ? dxil_emit_call(t->alu.module, function, args, 2) : NULL;
  return cast(t, DXIL_CAST_FPTOUI, 0, value);
}
static XeDxilValue unpack_depth(Transfer *t, unsigned format, XeDxilValue depth) {
  return format ? xe_dxil_unpack_depth20e4(&t->alu, depth, 1) :
    mul(t, cast(t, DXIL_CAST_UITOFP, 1, add(t, depth, shr(t, depth, 23))), fc(t, 1.0f / 16777216.0f));
}
static int store_output(Transfer *t, unsigned id, unsigned component, int integer_view, XeDxilValue value) {
  XeDxilValue args[5];
  const struct dxil_func *function = dxil_get_function(t->alu.module, "dx.op.storeOutput", integer_view ? DXIL_I32 : DXIL_F32);
  args[0] = ic(t, 5); args[1] = ic(t, id); args[2] = ic(t, 0);
  args[3] = dxil_module_get_int8_const(t->alu.module, (int8_t)component); args[4] = value;
  return function && value && dxil_emit_call_void(t->alu.module, function, args, 5);
}
static XeDxilValue tile_index(Transfer *t, XeDxilValue destination, XeDxilValue address) {
  return field(t, add(t, destination, bin(t, DXIL_BINOP_ASHR, address, ic(t, 20))), 0, 2047);
}
static XeDxilValue host_depth(Transfer *t, XeDxilValue destination_tile, XeDxilValue address,
                                XeDxilValue x, XeDxilValue y, XeDxilValue sample) {
  const XeDxilEdramTransferOptions *o = t->options;
  XeDxilValue offset, tile, pitch, values[4], host_sample, sample_y;
  unsigned width = 80 * t->destination_x_scale, height = 16 * t->destination_y_scale;
  Canonical c;
  if (t->host_copy) {
    if (o->destination_msaa == 2) x = bor(t, shl(t, x, 1), field(t, sample, 0, 1));
    if (o->destination_msaa) {
      sample_y = o->destination_msaa == 1 && o->msaa_2x_supported ? bin(t, DXIL_BINOP_XOR, sample, ic(t, 1)) : shr(t, sample, 1);
      y = bor(t, shl(t, y, 1), field(t, sample_y, 0, 1));
    }
    offset = shl(t, add(t, mul(t, destination_tile, ic(t, width * height)), add(t, mul(t, y, ic(t, width)), x)), 2);
    return cast(t, DXIL_CAST_BITCAST, 1, xe_dxil_raw_buffer_load(&t->alu, t->host, offset));
  }
  c = canonicalize(t, o->destination_msaa, x, y, sample, t->destination_x_scale, t->destination_y_scale);
  decanonicalize(t, o->host_depth_msaa, &c, t->destination_x_scale, t->destination_y_scale, &x, &y, &host_sample);
  tile = tile_index(t, destination_tile, address); pitch = field(t, address, 10, 1023);
  width >>= o->host_depth_msaa >= 2; height >>= o->host_depth_msaa >= 1;
  x = add(t, mul(t, bin(t, DXIL_BINOP_UREM, tile, pitch), ic(t, width)), x);
  y = add(t, mul(t, bin(t, DXIL_BINOP_UDIV, tile, pitch), ic(t, height)), y);
  return load_texture(t, t->host, o->host_depth_msaa, 0, x, y, host_sample, values) ? values[0] : NULL;
}

int xe_dxil_build_edram_transfer(const XeDxilEdramTransferOptions *o, XeDxilShaderBinary *binary_out) {
  Transfer t;
  Canonical c;
  XeDxilValue args[5], position[2], destination_x, destination_y, destination_sample, destination_tile;
  XeDxilValue x, y, sample, source_tile, source_pitch, address, host_address, half, host, depth;
  XeDxilValue source_values[4], first_values[4], stencil_values[4], packed[2], loaded[2], values[4], condition;
  XeDxilValue first_depth = NULL, first_stencil = NULL;
  const struct dxil_func *function;
  unsigned i, j, source_kind, host_kind, host_id, width, height, shift, source_width, source_height, count;
  unsigned destination_components = 0, source_storage, destination_storage;
  unsigned need_source, need_stencil, resource_count = 0;
  unsigned direct_color, gamma_color, direct_depth;
  int color_id = -1, depth_id = -1, stencil_id = -1, result = 0;
  if (!o || !binary_out || o->mode > 9 || o->source_msaa > 2 || o->destination_msaa > 2 || o->host_depth_msaa > 2 ||
      !o->scale_x || !o->scale_y || o->scale_x > 16 || o->scale_y > 16 || o->color_target > 3) return 0;
  memset(&t, 0, sizeof(t)); t.options = o;
  t.source_color = o->mode == 0 || o->mode == 1 || o->mode == 4 || o->mode == 6 || o->mode == 8;
  t.destination_color = o->mode == 1 || o->mode == 3;
  t.stencil_bit = o->mode == 4 || o->mode == 5;
  t.host_depth = o->mode >= 6; t.host_copy = o->mode >= 8;
  need_source = t.source_color || !t.stencil_bit;
  need_stencil = !t.source_color && (t.stencil_bit || t.destination_color || o->stencil_reference);
  if ((t.source_color ? !color_format_valid(o->source_format) : o->source_format > 1) ||
      (t.destination_color ? !color_format_valid(o->destination_format) : o->destination_format > 1) ||
      (t.host_copy && o->host_depth_msaa)) return 0;
  t.source_words = t.source_color ? color_words(o->source_format) : 1;
  t.destination_words = t.destination_color ? color_words(o->destination_format) : 1;
  source_storage = storage_format(o->source_format);
  destination_storage = storage_format(o->destination_format);
  direct_color = t.source_color && t.destination_color && t.source_words == t.destination_words &&
    o->source_uint == o->destination_uint && (source_storage == destination_storage ||
    (o->source_uint && ((o->source_format == 4 && o->destination_format == 6) ||
      (o->source_format == 6 && o->destination_format == 4) ||
      (o->source_format == 5 && o->destination_format == 7) ||
      (o->source_format == 7 && o->destination_format == 5))));
  gamma_color = t.source_color && t.destination_color && o->source_format <= 1 && o->destination_format <= 1;
  direct_depth = !t.source_color && !t.destination_color && !t.stencil_bit &&
    o->source_format == o->destination_format && !t.host_depth;
  t.source_x_scale = o->source_native ? 1 : o->scale_x; t.source_y_scale = o->source_native ? 1 : o->scale_y;
  t.destination_x_scale = o->destination_native ? 1 : o->scale_x; t.destination_y_scale = o->destination_native ? 1 : o->scale_y;
  t.shader = xe_dxil_shader_create(DXIL_PIXEL_SHADER);
  if (!t.shader) return 0;
  xe_dxil_alu_init(&t.alu, xe_dxil_shader_module(t.shader));
  if (xe_dxil_shader_signature(t.shader, 0, "SV_Position", 0, DXIL_SEM_POSITION, DXIL_PROG_SEM_POSITION,
      0, 0, 4, DXIL_INTERP_LINEAR_NOPERSPECTIVE) != 0) goto done;
  if (o->destination_msaa && !xe_dxil_shader_sample_frequency(t.shader)) goto done;
  if (t.destination_color) {
    destination_components = components(o->destination_format);
    color_id = xe_dxil_shader_signature_typed(t.shader, 1, "SV_Target", o->color_target, DXIL_SEM_TARGET,
      DXIL_PROG_SEM_TARGET, o->color_target, 0, destination_components, 0, o->destination_uint ? DXIL_COMP_TYPE_U32 : DXIL_COMP_TYPE_F32);
    if (color_id < 0) goto done;
  } else if (!t.stencil_bit) {
    depth_id = xe_dxil_shader_signature(t.shader, 1, "SV_Depth", 0, DXIL_SEM_DEPTH, DXIL_PROG_SEM_DEPTH,
      UINT32_MAX, 0, 1, 0);
    if (depth_id < 0) goto done;
    if (o->stencil_reference) {
      stencil_id = xe_dxil_shader_signature_typed(t.shader, 1, "SV_StencilRef", 0, DXIL_SEM_STENCIL_REF,
        DXIL_PROG_SEM_STENCIL_REF, UINT32_MAX, 0, 1, 0, DXIL_COMP_TYPE_U32);
      if (stencil_id < 0) goto done;
    }
  }
  source_kind = o->source_msaa ? DXIL_RESOURCE_KIND_TEXTURE2DMS : DXIL_RESOURCE_KIND_TEXTURE2D;
  if (need_source) {
    if (xe_dxil_shader_resource_typed(t.shader, XE_DXIL_RESOURCE_SRV, source_kind, o->source_space, 0, 1, 0,
        t.source_color && o->source_uint ? DXIL_COMP_TYPE_U32 : DXIL_COMP_TYPE_F32) != (int)resource_count) goto done;
    t.source = xe_dxil_resource_handle(&t.alu, XE_DXIL_RESOURCE_SRV, resource_count++, 0);
  }
  if (need_stencil) {
    if (xe_dxil_shader_resource_typed(t.shader, XE_DXIL_RESOURCE_SRV, source_kind,
        o->source_space, 1, 1, 0, DXIL_COMP_TYPE_U32) != (int)resource_count) goto done;
    t.stencil = xe_dxil_resource_handle(&t.alu, XE_DXIL_RESOURCE_SRV, resource_count++, 1);
  }
  if (xe_dxil_shader_resource(t.shader, XE_DXIL_RESOURCE_CBV, DXIL_RESOURCE_KIND_CBUFFER, 31, 1, 1, 16) != 0) goto done;
  if (t.host_depth) {
    host_id = resource_count;
    host_kind = t.host_copy ? DXIL_RESOURCE_KIND_RAW_BUFFER : o->host_depth_msaa ? DXIL_RESOURCE_KIND_TEXTURE2DMS : DXIL_RESOURCE_KIND_TEXTURE2D;
    if (xe_dxil_shader_resource(t.shader, XE_DXIL_RESOURCE_SRV, host_kind, o->host_depth_space, 0, 1, 0) != (int)host_id) goto done;
    t.host = xe_dxil_resource_handle(&t.alu, XE_DXIL_RESOURCE_SRV, host_id, 0);
  }
  t.constants = xe_dxil_resource_handle(&t.alu, XE_DXIL_RESOURCE_CBV, 0, 1);
  address = xe_dxil_cbuffer_word(&t.alu, t.constants, ic(&t, t.host_depth ? 1 : 0));
  host_address = t.host_depth ? xe_dxil_cbuffer_word(&t.alu, t.constants, ic(&t, 0)) : NULL;
  function = dxil_get_function(t.alu.module, "dx.op.loadInput", DXIL_F32);
  args[0] = ic(&t, 4); args[1] = ic(&t, 0); args[2] = args[1];
  args[4] = dxil_module_get_undef(t.alu.module, dxil_module_get_int_type(t.alu.module, 32));
  for (i = 0; i < 2; ++i) {
    args[3] = dxil_module_get_int8_const(t.alu.module, (int8_t)i);
    position[i] = function ? dxil_emit_call(t.alu.module, function, args, 5) : NULL;
    position[i] = cast(&t, DXIL_CAST_FPTOUI, 0, position[i]);
  }
  destination_sample = ic(&t, 0);
  if (o->destination_msaa) {
    function = dxil_get_function(t.alu.module, "dx.op.sampleIndex", DXIL_I32); args[0] = ic(&t, 90);
    destination_sample = function ? dxil_emit_call(t.alu.module, function, args, 1) : NULL;
  }
  width = (80 * t.destination_x_scale) >> ((o->destination_msaa >= 2) + (t.destination_words == 2));
  height = (16 * t.destination_y_scale) >> (o->destination_msaa >= 1);
  destination_x = bin(&t, DXIL_BINOP_UREM, position[0], ic(&t, width));
  destination_y = bin(&t, DXIL_BINOP_UREM, position[1], ic(&t, height));
  destination_tile = add(&t, mul(&t, bin(&t, DXIL_BINOP_UDIV, position[1], ic(&t, height)), field(&t, address, 0, 1023)),
    bin(&t, DXIL_BINOP_UDIV, position[0], ic(&t, width)));
  x = destination_x; y = destination_y;
  if (o->source_native != o->destination_native) {
    if (o->destination_native) {
      x = add(&t, mul(&t, x, ic(&t, t.source_x_scale)), ic(&t, t.source_x_scale / 2));
      y = add(&t, mul(&t, y, ic(&t, t.source_y_scale)), ic(&t, t.source_y_scale / 2));
    } else {
      x = bin(&t, DXIL_BINOP_UDIV, x, ic(&t, t.destination_x_scale));
      y = bin(&t, DXIL_BINOP_UDIV, y, ic(&t, t.destination_y_scale));
    }
  }
  half = ic(&t, 0); sample = destination_sample;
  if (o->source_msaa != o->destination_msaa || t.source_words != t.destination_words) {
    c = canonicalize(&t, o->destination_msaa, x, y, destination_sample, t.source_x_scale, t.source_y_scale);
    if (t.destination_words == 2 && t.source_words == 1) c.u = shl(&t, c.u, 1);
    else if (t.source_words == 2 && t.destination_words == 1) { half = field(&t, c.u, 0, 1); c.u = shr(&t, c.u, 1); }
    decanonicalize(&t, o->source_msaa, &c, t.source_x_scale, t.source_y_scale, &x, &y, &sample);
  }
  shift = (o->source_msaa >= 2) + (t.source_words == 2);
  if (t.source_color != t.destination_color) {
    width = (80 * t.source_x_scale) >> (1 + shift);
    x = add(&t, x, pick(&t, compare(&t, DXIL_ICMP_ULT, x, ic(&t, width)), ic(&t, width), ic(&t, 0u - width)));
  }
  source_tile = tile_index(&t, destination_tile, address); source_pitch = field(&t, address, 10, 1023);
  source_width = (80 * t.source_x_scale) >> shift;
  source_height = (16 * t.source_y_scale) >> (o->source_msaa >= 1);
  x = add(&t, mul(&t, bin(&t, DXIL_BINOP_UREM, source_tile, source_pitch), ic(&t, source_width)), x);
  y = add(&t, mul(&t, bin(&t, DXIL_BINOP_UDIV, source_tile, source_pitch), ic(&t, source_height)), y);
  if (t.alu.failed || (need_source && !t.source) || !t.constants || !address || !destination_sample || (need_stencil && !t.stencil) ||
      (t.host_depth && (!t.host || !host_address))) goto done;
  count = t.source_words == 1 && t.destination_words == 2 ? 2 : 1;
  packed[0] = ic(&t, 0); packed[1] = packed[0];
  for (i = 0; i < count; ++i) {
    for (j = 0; j < 4; ++j) source_values[j] = fc(&t, 0.0f);
    if (need_source && !load_texture(&t, t.source, o->source_msaa, t.source_color && o->source_uint,
        add(&t, x, ic(&t, i * t.source_x_scale)), y, sample, source_values)) goto done;
    if (!i) for (j = 0; j < 4; ++j) first_values[j] = source_values[j];
    loaded[0] = ic(&t, 0); loaded[1] = loaded[0];
    if (t.source_color) {
      if (!direct_color && !gamma_color &&
          xe_dxil_pack_edram_color(&t.alu, o->source_format, o->source_uint, source_values, loaded) != (int)t.source_words) goto done;
    } else {
      stencil_values[0] = ic(&t, 0);
      if (need_stencil && !load_texture(&t, t.stencil, o->source_msaa, 1, add(&t, x, ic(&t, i * t.source_x_scale)), y, sample, stencil_values)) goto done;
      if (!i) { first_depth = source_values[0]; first_stencil = stencil_values[0]; }
      if (!direct_depth) loaded[0] = bor(&t, need_source ? shl(&t, pack_depth(&t, o->source_format, source_values[0]), 8) : ic(&t, 0), field(&t, stencil_values[0], 0, 255));
      loaded[1] = ic(&t, 0);
    }
    if (count == 2) packed[i] = loaded[0];
    else if (t.source_words == 2 && t.destination_words == 1) packed[0] = pick(&t, compare(&t, DXIL_ICMP_NE, half, ic(&t, 0)), loaded[1], loaded[0]);
    else { packed[0] = loaded[0]; packed[1] = loaded[1]; }
  }
  if (t.alu.failed) goto done;
  if (t.stencil_bit) {
    if (!o->no_discard_stencil) {
      condition = compare(&t, DXIL_ICMP_EQ, bin(&t, DXIL_BINOP_AND, packed[0],
        xe_dxil_cbuffer_word(&t.alu, t.constants, ic(&t, 1))), ic(&t, 0));
      function = dxil_get_function(t.alu.module, "dx.op.discard", DXIL_NONE);
      args[0] = ic(&t, 82); args[1] = condition;
      if (!function || !condition || !dxil_emit_call_void(t.alu.module, function, args, 2)) goto done;
    }
  } else if (t.destination_color) {
    if (direct_color) {
      for (i = 0; i < destination_components; ++i) values[i] = first_values[i];
    } else if (gamma_color) {
      for (i = 0; i < 4; ++i) values[i] = i == 3 ? first_values[i] : o->destination_format ?
        xe_dxil_gamma_decode(&t.alu, first_values[i]) : xe_dxil_gamma_encode(&t.alu, first_values[i]);
    } else if (o->value_convert && t.source_color && source_storage == 3 && !o->destination_format) {
      if (!xe_dxil_unpack_edram_color(&t.alu, 3, 0, packed, values)) goto done;
      for (i = 0; i < 3; ++i) values[i] = xe_dxil_alu_saturate(&t.alu, values[i]);
    } else if (!xe_dxil_unpack_edram_color(&t.alu, o->destination_format, o->destination_uint, packed, values)) goto done;
    for (i = 0; i < destination_components; ++i)
      if (!store_output(&t, (unsigned)color_id, i, o->destination_uint, values[i])) goto done;
  } else {
    depth = shr(&t, packed[0], 8);
    if (direct_depth) values[0] = first_depth;
    else {
      values[0] = unpack_depth(&t, o->destination_format, depth);
      if (t.host_depth) {
        host = host_depth(&t, destination_tile, host_address, destination_x, destination_y, destination_sample);
        condition = compare(&t, DXIL_ICMP_EQ, pack_depth(&t, o->destination_format, host), depth);
        values[0] = pick(&t, condition, host, values[0]);
      }
    }
    if (!store_output(&t, (unsigned)depth_id, 0, 0, values[0])) goto done;
    if (stencil_id >= 0 && !store_output(&t, (unsigned)stencil_id, 0, 1,
        t.source_color ? field(&t, packed[0], 0, 255) : first_stencil)) goto done;
  }
  if (!t.alu.failed && dxil_emit_ret_void(t.alu.module)) result = xe_dxil_shader_finish(t.shader, binary_out);
done:
  xe_dxil_shader_destroy(t.shader);
  return result;
}
