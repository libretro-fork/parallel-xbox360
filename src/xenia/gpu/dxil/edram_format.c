/* Raw DXIL render-target packing, shared by dump, transfer and ROP paths.
 * Format rules follow Xenia's EDRAM emitters (Ben Vanik and contributors).
 * MSVC C89. BSD license, see LICENSE. */
#include "edram_format.h"
#include <dxil_module.h>
#include <dxil_function.h>

static XeDxilValue ic(XeDxilAlu *s, uint32_t value) { return dxil_module_get_int32_const(s->module, (int32_t)value); }
static XeDxilValue fc(XeDxilAlu *s, float value) { return dxil_module_get_float_const(s->module, value); }
static XeDxilValue bin(XeDxilAlu *s, enum dxil_bin_opcode op, XeDxilValue a, XeDxilValue b) {
  XeDxilValue value = a && b ? dxil_emit_binop(s->module, op, a, b, 0) : NULL;
  if (!value) s->failed = 1;
  return value;
}
static XeDxilValue cmp(XeDxilAlu *s, enum dxil_cmp_pred op, XeDxilValue a, XeDxilValue b) {
  XeDxilValue value = a && b ? dxil_emit_cmp(s->module, op, a, b) : NULL;
  if (!value) s->failed = 1;
  return value;
}
static XeDxilValue choose(XeDxilAlu *s, XeDxilValue condition, XeDxilValue a, XeDxilValue b) {
  XeDxilValue value = condition && a && b ? dxil_emit_select(s->module, condition, a, b) : NULL;
  if (!value) s->failed = 1;
  return value;
}
static XeDxilValue cast(XeDxilAlu *s, enum dxil_cast_opcode op, int floating, XeDxilValue value) {
  value = value ? dxil_emit_cast(s->module, op, floating ? dxil_module_get_float_type(s->module, 32) :
    dxil_module_get_int_type(s->module, 32), value) : NULL;
  if (!value) s->failed = 1;
  return value;
}
static XeDxilValue field(XeDxilAlu *s, XeDxilValue value, unsigned shift, uint32_t mask) {
  return bin(s, DXIL_BINOP_AND, shift ? bin(s, DXIL_BINOP_LSHR, value, ic(s, shift)) : value, ic(s, mask));
}
static int ready(XeDxilAlu *s, XeDxilValue value) {
  return s && s->module && !s->failed && s->module->cur_emitting_func && value;
}
static XeDxilValue pack_small_float(XeDxilAlu *s, XeDxilValue value, unsigned bias,
                                     unsigned shift, uint32_t mask, int round_even) {
  XeDxilValue bits, denormal, normal, amount, biased;
  bits = cast(s, DXIL_CAST_BITCAST, 0, value);
  amount = bin(s, DXIL_BINOP_SUB, ic(s, bias + 1), bin(s, DXIL_BINOP_LSHR, bits, ic(s, 23)));
  amount = choose(s, cmp(s, DXIL_ICMP_ULT, amount, ic(s, 24)), amount, ic(s, 24));
  denormal = bin(s, DXIL_BINOP_LSHR, bin(s, DXIL_BINOP_OR, field(s, bits, 0, 0x7fffff), ic(s, 0x800000)), amount);
  normal = bin(s, DXIL_BINOP_SUB, bits, ic(s, bias << 23));
  biased = choose(s, cmp(s, DXIL_ICMP_ULT, bits, ic(s, (bias + 1) << 23)), denormal, normal);
  if (round_even) biased = bin(s, DXIL_BINOP_ADD,
    bin(s, DXIL_BINOP_ADD, biased, ic(s, (1u << (shift - 1)) - 1)), field(s, biased, shift, 1));
  return field(s, biased, shift, mask);
}
static XeDxilValue unpack_small_float(XeDxilAlu *s, XeDxilValue value, unsigned mantissa,
                                       uint32_t exponent_mask, unsigned bias, float denormal_scale) {
  XeDxilValue exponent = field(s, value, mantissa, exponent_mask);
  XeDxilValue fraction = field(s, value, 0, (1u << mantissa) - 1);
  XeDxilValue normal = bin(s, DXIL_BINOP_OR,
    bin(s, DXIL_BINOP_SHL, bin(s, DXIL_BINOP_ADD, exponent, ic(s, bias)), ic(s, 23)),
    bin(s, DXIL_BINOP_SHL, fraction, ic(s, 23 - mantissa)));
  return choose(s, cmp(s, DXIL_ICMP_NE, exponent, ic(s, 0)), cast(s, DXIL_CAST_BITCAST, 1, normal),
    bin(s, DXIL_BINOP_MUL, cast(s, DXIL_CAST_UITOFP, 1, fraction), fc(s, denormal_scale)));
}
XeDxilValue xe_dxil_pack_float7e3(XeDxilAlu *s, XeDxilValue value) {
  if (!ready(s, value)) return NULL;
  value = choose(s, cmp(s, DXIL_FCMP_OGT, value, fc(s, 0.0f)), value, fc(s, 0.0f));
  value = choose(s, cmp(s, DXIL_FCMP_OLT, value, fc(s, 31.875f)), value, fc(s, 31.875f));
  return pack_small_float(s, value, 124, 16, 1023, 1);
}
XeDxilValue xe_dxil_unpack_float7e3(XeDxilAlu *s, XeDxilValue value) {
  return ready(s, value) ? unpack_small_float(s, value, 7, 7, 124, 0.001953125f) : NULL;
}
XeDxilValue xe_dxil_pack_depth20e4(XeDxilAlu *s, XeDxilValue value, int round_even, int remap) {
  return ready(s, value) ? pack_small_float(s, value, remap ? 111 : 112, 3, 0xffffff, round_even) : NULL;
}
XeDxilValue xe_dxil_unpack_depth20e4(XeDxilAlu *s, XeDxilValue value, int remap) {
  return ready(s, value) ? unpack_small_float(s, value, 20, 15, remap ? 111 : 112,
    remap ? 2.910383045673370361328125e-11f : 5.82076609134674072265625e-11f) : NULL;
}

static XeDxilValue linear_to_gamma(XeDxilAlu *s, XeDxilValue value) {
  XeDxilValue condition, scale, offset, args[2];
  const struct dxil_func *function;
  scale = fc(s, 1023.0f); offset = fc(s, 0.0f);
  condition = cmp(s, DXIL_FCMP_OGE, value, fc(s, 64.0f / 1023.0f));
  scale = choose(s, condition, fc(s, 511.5f), scale); offset = choose(s, condition, fc(s, 32.0f / 255.0f), offset);
  condition = cmp(s, DXIL_FCMP_OGE, value, fc(s, 128.0f / 1023.0f));
  scale = choose(s, condition, fc(s, 255.75f), scale); offset = choose(s, condition, fc(s, 64.0f / 255.0f), offset);
  condition = cmp(s, DXIL_FCMP_OGE, value, fc(s, 512.0f / 1023.0f));
  scale = choose(s, condition, fc(s, 127.875f), scale); offset = choose(s, condition, fc(s, 128.0f / 255.0f), offset);
  args[0] = ic(s, 29); args[1] = bin(s, DXIL_BINOP_MUL, value, scale);
  function = dxil_get_function(s->module, "dx.op.unary", DXIL_F32);
  value = function && args[1] ? dxil_emit_call(s->module, function, args, 2) : NULL;
  return bin(s, DXIL_BINOP_ADD, bin(s, DXIL_BINOP_MUL, value, fc(s, 1.0f / 255.0f)), offset);
}
static XeDxilValue gamma_to_linear(XeDxilAlu *s, XeDxilValue byte) {
  static const unsigned thresholds[] = {64,96,192};
  static const float reciprocals[] = {1.0f/511.5f,1.0f/255.75f,1.0f/127.875f};
  static const float offsets[] = {-31.5f/511.5f,-63.5f/255.75f,-127.5f/127.875f};
  XeDxilValue condition, scale = fc(s, 1.0f / 1023.0f), offset = fc(s, 0.5f / 1023.0f);
  unsigned i;
  for (i = 0; i < 3; ++i) {
    condition = cmp(s, DXIL_ICMP_UGE, byte, ic(s, thresholds[i]));
    scale = choose(s, condition, fc(s, reciprocals[i]), scale);
    offset = choose(s, condition, fc(s, offsets[i]), offset);
  }
  return bin(s, DXIL_BINOP_ADD, bin(s, DXIL_BINOP_MUL, cast(s, DXIL_CAST_UITOFP, 1, byte), scale), offset);
}
XeDxilValue xe_dxil_gamma_encode(XeDxilAlu *s, XeDxilValue value) {
  return ready(s, value) ? linear_to_gamma(s, value) : NULL;
}
XeDxilValue xe_dxil_gamma_decode(XeDxilAlu *s, XeDxilValue value) {
  XeDxilValue condition, scale, offset, args[2], linear, truncated;
  const struct dxil_func *function;
  if (!ready(s, value)) return NULL;
  scale = fc(s, 1.0f / 1024.0f); offset = fc(s, 0.0f);
  condition = cmp(s, DXIL_FCMP_OGE, value, fc(s, 64.0f / 255.0f));
  scale = choose(s, condition, fc(s, 2.0f / 1024.0f), scale); offset = choose(s, condition, fc(s, -64.0f), offset);
  condition = cmp(s, DXIL_FCMP_OGE, value, fc(s, 96.0f / 255.0f));
  scale = choose(s, condition, fc(s, 4.0f / 1024.0f), scale); offset = choose(s, condition, fc(s, -256.0f), offset);
  condition = cmp(s, DXIL_FCMP_OGE, value, fc(s, 192.0f / 255.0f));
  scale = choose(s, condition, fc(s, 8.0f / 1024.0f), scale); offset = choose(s, condition, fc(s, -1024.0f), offset);
  linear = bin(s, DXIL_BINOP_ADD, bin(s, DXIL_BINOP_MUL,
    bin(s, DXIL_BINOP_MUL, value, fc(s, 255.0f * 1024.0f)), scale), offset);
  args[0] = ic(s, 29); args[1] = bin(s, DXIL_BINOP_MUL, linear, scale);
  function = dxil_get_function(s->module, "dx.op.unary", DXIL_F32);
  truncated = function && args[1] ? dxil_emit_call(s->module, function, args, 2) : NULL;
  return bin(s, DXIL_BINOP_MUL, bin(s, DXIL_BINOP_ADD, linear, truncated), fc(s, 1.0f / 1023.0f));
}
static int word_count(unsigned format) {
  switch (format) {
  case 0: case 1: case 2: case 3: case 4: case 6: case 10: case 12: case 14: return 1;
  case 5: case 7: case 15: return 2;
  default: return 0;
  }
}
int xe_dxil_pack_edram_color(XeDxilAlu *s, unsigned format, int source_uint,
                               XeDxilValue source[4], XeDxilValue packed[2]) {
  unsigned i, width, shift, word, components;
  int count = word_count(format);
  XeDxilValue value;
  if (!s || !source || !packed || !count || s->failed) return 0;
  packed[0] = ic(s, 0); packed[1] = ic(s, 0);
  components = format == 14 ? 1 : format == 4 || format == 6 || format == 15 ? 2 : 4;
  for (i = 0; i < components; ++i) {
    value = source[i]; if (!ready(s, value)) goto fail;
    if (format >= 14) { packed[i] = source_uint ? value : cast(s, DXIL_CAST_BITCAST, 0, value); continue; }
    if (format >= 4 && format <= 7) {
      if (!source_uint) goto fail;
      width = 16; word = i / 2; shift = (i & 1) * 16;
    } else {
      if (source_uint) goto fail;
      word = 0; width = format < 2 ? 8 : i < 3 ? 10 : 2; shift = format < 2 ? i * 8 : i * 10;
      if (i < 3 && (format == 3 || format == 12)) value = xe_dxil_pack_float7e3(s, value);
      else {
        if (format == 1 && i < 3) value = linear_to_gamma(s, value);
        if (i == 3 && (format == 3 || format == 12)) value = xe_dxil_alu_saturate(s, value);
        value = cast(s, DXIL_CAST_FPTOUI, 0, bin(s, DXIL_BINOP_ADD,
          bin(s, DXIL_BINOP_MUL, value, fc(s, (float)((1u << width) - 1))), fc(s, 0.5f)));
      }
    }
    value = field(s, value, 0, (1u << width) - 1);
    packed[word] = bin(s, DXIL_BINOP_OR, packed[word], shift ? bin(s, DXIL_BINOP_SHL, value, ic(s, shift)) : value);
  }
  return s->failed ? 0 : count;
fail:
  s->failed = 1; return 0;
}
int xe_dxil_unpack_edram_color(XeDxilAlu *s, unsigned format, int destination_uint,
                                 XeDxilValue packed[2], XeDxilValue destination[4]) {
  unsigned i, width, shift, components;
  int count = word_count(format);
  XeDxilValue value;
  if (!s || !packed || !destination || !count || s->failed) return 0;
  components = format == 14 ? 1 : format == 4 || format == 6 || format == 15 ? 2 : 4;
  for (i = 0; i < 4; ++i) {
    destination[i] = destination_uint ? ic(s, 0) : fc(s, 0.0f);
    if (i >= components) continue;
    if (format >= 14) { destination[i] = destination_uint ? packed[i] : cast(s, DXIL_CAST_BITCAST, 1, packed[i]); continue; }
    if (format >= 4 && format <= 7) {
      if (!destination_uint) goto fail;
      destination[i] = field(s, packed[i / 2], (i & 1) * 16, 65535); continue;
    }
    if (destination_uint) goto fail;
    width = format < 2 ? 8 : i < 3 ? 10 : 2; shift = format < 2 ? i * 8 : i * 10;
    value = field(s, packed[0], shift, (1u << width) - 1);
    if (i < 3 && (format == 3 || format == 12)) value = xe_dxil_unpack_float7e3(s, value);
    else if (format == 1 && i < 3) value = gamma_to_linear(s, value);
    else value = bin(s, DXIL_BINOP_MUL, cast(s, DXIL_CAST_UITOFP, 1, value), fc(s, 1.0f / (float)((1u << width) - 1)));
    destination[i] = value;
  }
  return s->failed ? 0 : count;
fail:
  s->failed = 1; return 0;
}
