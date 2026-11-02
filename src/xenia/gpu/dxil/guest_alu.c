/* Xenia: Xbox 360 ALU lowering to raw DXIL. BSD license; see LICENSE.
 * Based on guest arithmetic rules documented by Ben Vanik and contributors.
 * Copyright 2020 Ben Vanik. All rights reserved.
 * Arithmetic behavior follows ucode.h and the existing guest translator.
 * New implementation uses MSVC-compatible C89, with no SPIR-V dependency. */
#include "guest_alu.h"
#include <dxil_module.h>
#include <dxil_function.h>
#include <float.h>

static XeDxilValue checked(XeDxilAlu *s, XeDxilValue value) {
  if (!value) s->failed = 1;
  return value;
}

static XeDxilValue fc(XeDxilAlu *s, float value) {
  return checked(s, dxil_module_get_float_const(s->module, value));
}

static XeDxilValue ic(XeDxilAlu *s, uint32_t value) {
  return checked(s, dxil_module_get_int32_const(s->module, (int32_t)value));
}

static XeDxilValue bin(XeDxilAlu *s, enum dxil_bin_opcode op,
                       XeDxilValue a, XeDxilValue b) {
  if (!a || !b || s->failed) { s->failed = 1; return NULL; }
  return checked(s, dxil_emit_binop(s->module, op, a, b, 0));
}

static XeDxilValue cmp(XeDxilAlu *s, enum dxil_cmp_pred op,
                       XeDxilValue a, XeDxilValue b) {
  if (!a || !b || s->failed) { s->failed = 1; return NULL; }
  return checked(s, dxil_emit_cmp(s->module, op, a, b));
}

static XeDxilValue choose(XeDxilAlu *s, XeDxilValue condition,
                          XeDxilValue a, XeDxilValue b) {
  if (!condition || !a || !b || s->failed) { s->failed = 1; return NULL; }
  return checked(s, dxil_emit_select(s->module, condition, a, b));
}

static XeDxilValue cast(XeDxilAlu *s, enum dxil_cast_opcode op,
                        const struct dxil_type *type, XeDxilValue value) {
  if (!value || !type || s->failed) { s->failed = 1; return NULL; }
  return checked(s, dxil_emit_cast(s->module, op, type, value));
}

static XeDxilValue bits(XeDxilAlu *s, XeDxilValue value) {
  return cast(s, DXIL_CAST_BITCAST, dxil_module_get_int_type(s->module, 32), value);
}

static XeDxilValue from_bits(XeDxilAlu *s, XeDxilValue value) {
  return cast(s, DXIL_CAST_BITCAST, dxil_module_get_float_type(s->module, 32), value);
}

XeDxilValue xe_dxil_alu_float_bits(XeDxilAlu *s, uint32_t value) {
  return from_bits(s, ic(s, value));
}

static XeDxilValue unary(XeDxilAlu *s, unsigned opcode, XeDxilValue a) {
  const struct dxil_func *function;
  XeDxilValue args[2];
  if (!a || s->failed) { s->failed = 1; return NULL; }
  function = dxil_get_function(s->module, "dx.op.unary", DXIL_F32);
  args[0] = ic(s, opcode); args[1] = a;
  if (!function || !args[0]) { s->failed = 1; return NULL; }
  return checked(s, dxil_emit_call(s->module, function, args, 2));
}

static XeDxilValue numeric_minmax(XeDxilAlu *s, unsigned opcode,
                                  XeDxilValue a, XeDxilValue b) {
  const struct dxil_func *function;
  XeDxilValue args[3];
  if (!a || !b || s->failed) { s->failed = 1; return NULL; }
  function = dxil_get_function(s->module, "dx.op.binary", DXIL_F32);
  args[0] = ic(s, opcode); args[1] = a; args[2] = b;
  if (!function || !args[0]) { s->failed = 1; return NULL; }
  return checked(s, dxil_emit_call(s->module, function, args, 3));
}

static XeDxilValue neg(XeDxilAlu *s, XeDxilValue a) {
  return from_bits(s, bin(s, DXIL_BINOP_XOR, bits(s, a), ic(s, UINT32_C(0x80000000))));
}

XeDxilValue xe_dxil_alu_modify(XeDxilAlu *s, XeDxilValue value,
                              int absolute_value, int negate_value) {
  if (!value || s->failed) { s->failed = 1; return NULL; }
  if (absolute_value) value = unary(s, 6, value);
  return negate_value ? neg(s, value) : value;
}

XeDxilValue xe_dxil_alu_saturate(XeDxilAlu *s, XeDxilValue value) {
  return unary(s, 7, value);
}

/* Shader Model 3 multiplication returns +0 if either distinct operand is
 * zero/denormal, even if the other is NaN. The identical-operand path keeps
 * normal IEEE multiplication, matching the existing translator's shortcut. */
static XeDxilValue multiply(XeDxilAlu *s, XeDxilValue a, XeDxilValue b,
                             int identical) {
  XeDxilValue product;
  XeDxilValue za;
  XeDxilValue zb;
  product = bin(s, DXIL_BINOP_MUL, a, b);
  if (identical) return product;
  za = cmp(s, DXIL_ICMP_ULT,
    bin(s, DXIL_BINOP_AND, bits(s, a), ic(s, UINT32_C(0x7fffffff))),
    ic(s, UINT32_C(0x00800000)));
  zb = cmp(s, DXIL_ICMP_ULT,
    bin(s, DXIL_BINOP_AND, bits(s, b), ic(s, UINT32_C(0x7fffffff))),
    ic(s, UINT32_C(0x00800000)));
  return choose(s, bin(s, DXIL_BINOP_OR, za, zb), fc(s, 0.0f), product);
}

static XeDxilValue address_value(XeDxilAlu *s, XeDxilValue value,
                                  int round_nearest, float low) {
  if (round_nearest) value = bin(s, DXIL_BINOP_ADD, value, fc(s, 0.5f));
  value = unary(s, 27, value);
  value = numeric_minmax(s, 35, value, fc(s, low));
  value = numeric_minmax(s, 36, value, fc(s, 255.0f));
  return cast(s, DXIL_CAST_FPTOSI, dxil_module_get_int_type(s->module, 32), value);
}

void xe_dxil_alu_init(XeDxilAlu *s, struct dxil_module *module) {
  memset(s, 0, sizeof(*s));
  s->module = module;
  s->previous_scalar = fc(s, 0.0f);
  s->predicate = checked(s, dxil_module_get_int1_const(module, 0));
  s->killed = s->predicate;
  s->operation_kill = s->predicate;
  s->address = ic(s, 0);
}

static int cube(XeDxilAlu *s, XeDxilValue a[4], XeDxilValue out[4]) {
  XeDxilValue x = a[2];
  XeDxilValue y = a[3];
  XeDxilValue z = a[0];
  XeDxilValue abs_x;
  XeDxilValue abs_y;
  XeDxilValue abs_z;
  XeDxilValue major_z;
  XeDxilValue major_y;
  XeDxilValue negative_x;
  XeDxilValue negative_y;
  XeDxilValue negative_z;
  XeDxilValue yz[4];
  XeDxilValue y_values[4];
  XeDxilValue x_values[4];
  XeDxilValue z_values[4];
  unsigned i;
  abs_x = unary(s, 6, x); abs_y = unary(s, 6, y); abs_z = unary(s, 6, z);
  major_z = bin(s, DXIL_BINOP_AND, cmp(s, DXIL_FCMP_OGE, abs_z, abs_x),
    cmp(s, DXIL_FCMP_OGE, abs_z, abs_y));
  major_y = cmp(s, DXIL_FCMP_OGE, abs_y, abs_x);
  negative_x = cmp(s, DXIL_FCMP_OLT, x, fc(s, 0.0f));
  negative_y = cmp(s, DXIL_FCMP_OLT, y, fc(s, 0.0f));
  negative_z = cmp(s, DXIL_FCMP_OLT, z, fc(s, 0.0f));
  z_values[0] = neg(s, y);
  z_values[1] = choose(s, negative_z, neg(s, x), x);
  z_values[2] = z;
  z_values[3] = choose(s, negative_z, fc(s, 5.0f), fc(s, 4.0f));
  y_values[0] = choose(s, negative_y, neg(s, z), z);
  y_values[1] = x; y_values[2] = y;
  y_values[3] = choose(s, negative_y, fc(s, 3.0f), fc(s, 2.0f));
  x_values[0] = z_values[0];
  x_values[1] = choose(s, negative_x, z, neg(s, z));
  x_values[2] = x;
  x_values[3] = choose(s, negative_x, fc(s, 1.0f), fc(s, 0.0f));
  for (i = 0; i < 4; ++i) {
    yz[i] = choose(s, major_y, y_values[i], x_values[i]);
    out[i] = choose(s, major_z, z_values[i], yz[i]);
  }
  out[2] = bin(s, DXIL_BINOP_MUL, out[2], fc(s, 2.0f));
  return !s->failed;
}

int xe_dxil_alu_vector(XeDxilAlu *s, unsigned opcode,
                       XeDxilValue a[3][4], unsigned identical,
                       XeDxilValue out[4]) {
  static const enum dxil_cmp_pred comparisons[4] = {
    DXIL_FCMP_OEQ, DXIL_FCMP_OGT, DXIL_FCMP_OGE, DXIL_FCMP_UNE
  };
  static const enum dxil_cmp_pred push_comparisons[4] = {
    DXIL_FCMP_OEQ, DXIL_FCMP_UNE, DXIL_FCMP_OGT, DXIL_FCMP_OGE
  };
  XeDxilValue value = NULL;
  XeDxilValue condition;
  XeDxilValue term;
  unsigned i;
  unsigned count;
  if (s->failed || opcode > XE_DXIL_V_MAXA) { s->failed = 1; return 0; }
  if (opcode == XE_DXIL_V_CUBE) return cube(s, a[0], out);
  if (opcode >= XE_DXIL_V_DP4 && opcode <= XE_DXIL_V_DP2ADD) {
    count = opcode == XE_DXIL_V_DP4 ? 4 : (opcode == XE_DXIL_V_DP3 ? 3 : 2);
    for (i = 0; i < count; ++i) {
      term = multiply(s, a[0][i], a[1][i], (identical & (1u << i)) != 0);
      value = i ? bin(s, DXIL_BINOP_ADD, value, term) : term;
    }
    if (opcode == XE_DXIL_V_DP2ADD) value = bin(s, DXIL_BINOP_ADD, value, a[2][0]);
  } else if (opcode == XE_DXIL_V_MAX4) {
    value = a[0][0];
    for (i = 1; i < 4; ++i) value = numeric_minmax(s, 35, value, a[0][i]);
  } else if (opcode >= XE_DXIL_V_SETP_EQ_PUSH && opcode <= XE_DXIL_V_SETP_GE_PUSH) {
    s->predicate = bin(s, DXIL_BINOP_AND,
      cmp(s, DXIL_FCMP_OEQ, a[0][3], fc(s, 0.0f)),
      cmp(s, push_comparisons[opcode - XE_DXIL_V_SETP_EQ_PUSH], a[1][3], fc(s, 0.0f)));
    condition = bin(s, DXIL_BINOP_AND,
      cmp(s, DXIL_FCMP_OEQ, a[0][0], fc(s, 0.0f)),
      cmp(s, push_comparisons[opcode - XE_DXIL_V_SETP_EQ_PUSH], a[1][0], fc(s, 0.0f)));
    value = bin(s, DXIL_BINOP_ADD,
      choose(s, condition, fc(s, -1.0f), a[0][0]), fc(s, 1.0f));
  } else if (opcode >= XE_DXIL_V_KILL_EQ && opcode <= XE_DXIL_V_KILL_NE) {
    condition = checked(s, dxil_module_get_int1_const(s->module, 0));
    for (i = 0; i < 4; ++i) condition = bin(s, DXIL_BINOP_OR, condition,
      cmp(s, comparisons[opcode - XE_DXIL_V_KILL_EQ], a[0][i], a[1][i]));
    s->operation_kill = condition;
    s->killed = bin(s, DXIL_BINOP_OR, s->killed, condition);
    value = choose(s, condition, fc(s, 1.0f), fc(s, 0.0f));
  }
  if (value) {
    for (i = 0; i < 4; ++i) out[i] = value;
    return !s->failed;
  }
  if (opcode == XE_DXIL_V_MAXA) s->address = address_value(s, a[0][3], 1, -256.0f);
  for (i = 0; i < 4; ++i) {
    switch (opcode) {
      case XE_DXIL_V_ADD: out[i] = bin(s, DXIL_BINOP_ADD, a[0][i], a[1][i]); break;
      case XE_DXIL_V_MUL:
      case XE_DXIL_V_MAD:
        out[i] = multiply(s, a[0][i], a[1][i], (identical & (1u << i)) != 0);
        if (opcode == XE_DXIL_V_MAD) out[i] = bin(s, DXIL_BINOP_ADD, out[i], a[2][i]);
        break;
      case XE_DXIL_V_MAX:
      case XE_DXIL_V_MIN:
      case XE_DXIL_V_MAXA:
        out[i] = (identical & (1u << i)) ? a[0][i] :
          choose(s, cmp(s, opcode == XE_DXIL_V_MIN ? DXIL_FCMP_OLT : DXIL_FCMP_OGE,
            a[0][i], a[1][i]), a[0][i], a[1][i]);
        break;
      case XE_DXIL_V_SEQ: case XE_DXIL_V_SGT: case XE_DXIL_V_SGE: case XE_DXIL_V_SNE:
        out[i] = choose(s, cmp(s, comparisons[opcode - XE_DXIL_V_SEQ], a[0][i], a[1][i]),
          fc(s, 1.0f), fc(s, 0.0f));
        break;
      case XE_DXIL_V_FRC: case XE_DXIL_V_TRUNC: case XE_DXIL_V_FLOOR:
        out[i] = unary(s, opcode == XE_DXIL_V_FRC ? 22 : (opcode == XE_DXIL_V_TRUNC ? 29 : 27), a[0][i]);
        break;
      case XE_DXIL_V_CNDEQ: case XE_DXIL_V_CNDGE: case XE_DXIL_V_CNDGT:
        condition = cmp(s, opcode == XE_DXIL_V_CNDEQ ? DXIL_FCMP_OEQ :
          (opcode == XE_DXIL_V_CNDGE ? DXIL_FCMP_OGE : DXIL_FCMP_OGT), a[0][i], fc(s, 0.0f));
        out[i] = choose(s, condition, a[1][i], a[2][i]);
        break;
      case XE_DXIL_V_DST:
        if (!i) out[i] = fc(s, 1.0f);
        else if (i == 1) out[i] = multiply(s, a[0][1], a[1][1], (identical & 2) != 0);
        else out[i] = i == 2 ? a[0][2] : a[1][3];
        break;
      default: s->failed = 1; return 0;
    }
    if (!out[i]) s->failed = 1;
  }
  return !s->failed;
}

/* Round the approximation to 21 mantissa bits, ties away from zero. Preserve
 * NaNs, infinities and signed zero; do not round a finite value to infinity. */
static XeDxilValue approximate(XeDxilAlu *s, XeDxilValue value) {
  XeDxilValue raw;
  XeDxilValue truncated;
  XeDxilValue rounded;
  XeDxilValue finite;
  XeDxilValue overflow;
  XeDxilValue round_up;
  raw = bits(s, value);
  truncated = bin(s, DXIL_BINOP_AND, raw, ic(s, UINT32_C(0xfffffffc)));
  round_up = cmp(s, DXIL_ICMP_UGE,
    bin(s, DXIL_BINOP_AND, raw, ic(s, 3)), ic(s, 2));
  rounded = bin(s, DXIL_BINOP_ADD, truncated, ic(s, 4));
  finite = cmp(s, DXIL_ICMP_NE,
    bin(s, DXIL_BINOP_AND, raw, ic(s, UINT32_C(0x7f800000))),
    ic(s, UINT32_C(0x7f800000)));
  overflow = cmp(s, DXIL_ICMP_EQ,
    bin(s, DXIL_BINOP_AND, rounded, ic(s, UINT32_C(0x7f800000))),
    ic(s, UINT32_C(0x7f800000)));
  rounded = choose(s, bin(s, DXIL_BINOP_AND, finite, overflow), truncated, rounded);
  rounded = choose(s, round_up, rounded, truncated);
  return from_bits(s, choose(s, finite, rounded, raw));
}

static void split(XeDxilAlu *s, XeDxilValue value,
                    XeDxilValue *high, XeDxilValue *low) {
  XeDxilValue scaled;
  scaled = bin(s, DXIL_BINOP_MUL, value, fc(s, 4097.0f));
  *high = bin(s, DXIL_BINOP_SUB, scaled, bin(s, DXIL_BINOP_SUB, scaled, value));
  *low = bin(s, DXIL_BINOP_SUB, value, *high);
}

static XeDxilValue mulsc(XeDxilAlu *s, XeDxilValue a, XeDxilValue b,
                          int identical) {
  XeDxilValue product;
  XeDxilValue ah;
  XeDxilValue al;
  XeDxilValue bh;
  XeDxilValue bl;
  XeDxilValue error;
  XeDxilValue away;
  XeDxilValue adjusted;
  product = bin(s, DXIL_BINOP_MUL, a, b);
  if (s->mulsc_round_toward_zero) {
    split(s, a, &ah, &al); split(s, b, &bh, &bl);
    error = bin(s, DXIL_BINOP_SUB, bin(s, DXIL_BINOP_MUL, ah, bh), product);
    error = bin(s, DXIL_BINOP_ADD, error, bin(s, DXIL_BINOP_MUL, ah, bl));
    error = bin(s, DXIL_BINOP_ADD, error, bin(s, DXIL_BINOP_MUL, al, bh));
    error = bin(s, DXIL_BINOP_ADD, error, bin(s, DXIL_BINOP_MUL, al, bl));
    away = bin(s, DXIL_BINOP_XOR,
      cmp(s, DXIL_FCMP_OLT, error, fc(s, 0.0f)),
      cmp(s, DXIL_FCMP_OLT, product, fc(s, 0.0f)));
    away = bin(s, DXIL_BINOP_AND, away, cmp(s, DXIL_FCMP_ONE, error, fc(s, 0.0f)));
    adjusted = from_bits(s, bin(s, DXIL_BINOP_SUB, bits(s, product), ic(s, 1)));
    product = choose(s, away, adjusted, product);
  }
  if (!identical) {
    XeDxilValue zero_a;
    XeDxilValue zero_b;
    zero_a = cmp(s, DXIL_ICMP_ULT,
      bin(s, DXIL_BINOP_AND, bits(s, a), ic(s, UINT32_C(0x7fffffff))), ic(s, UINT32_C(0x00800000)));
    zero_b = cmp(s, DXIL_ICMP_ULT,
      bin(s, DXIL_BINOP_AND, bits(s, b), ic(s, UINT32_C(0x7fffffff))), ic(s, UINT32_C(0x00800000)));
    product = choose(s, bin(s, DXIL_BINOP_OR, zero_a, zero_b), fc(s, 0.0f), product);
  }
  return product;
}

XeDxilValue xe_dxil_alu_scalar(XeDxilAlu *s, unsigned opcode,
                              XeDxilValue a, XeDxilValue b, int identical) {
  static const enum dxil_cmp_pred comparisons[4] = {
    DXIL_FCMP_OEQ, DXIL_FCMP_OGT, DXIL_FCMP_OGE, DXIL_FCMP_UNE
  };
  static const enum dxil_cmp_pred predicate_comparisons[4] = {
    DXIL_FCMP_OEQ, DXIL_FCMP_UNE, DXIL_FCMP_OGT, DXIL_FCMP_OGE
  };
  XeDxilValue value = NULL;
  XeDxilValue condition;
  XeDxilValue prior;
  XeDxilValue positive_inf;
  XeDxilValue negative_inf;
  unsigned intrinsic;
  if (s->failed || opcode > XE_DXIL_S_RETAIN_PREV || opcode == 41) {
    s->failed = 1; return NULL;
  }
  prior = s->previous_scalar;
  switch (opcode) {
    case XE_DXIL_S_ADD: case XE_DXIL_S_SUB:
    case XE_DXIL_S_ADDSC0: case XE_DXIL_S_ADDSC1:
    case XE_DXIL_S_SUBSC0: case XE_DXIL_S_SUBSC1:
      value = bin(s, opcode == XE_DXIL_S_SUB || opcode >= XE_DXIL_S_SUBSC0 ?
        DXIL_BINOP_SUB : DXIL_BINOP_ADD, a, b);
      break;
    case XE_DXIL_S_ADD_PREV: case XE_DXIL_S_SUB_PREV:
      value = bin(s, opcode == XE_DXIL_S_ADD_PREV ? DXIL_BINOP_ADD : DXIL_BINOP_SUB, a, prior);
      break;
    case XE_DXIL_S_MUL: value = multiply(s, a, b, identical); break;
    case XE_DXIL_S_MUL_PREV: value = multiply(s, a, prior, 0); break;
    case XE_DXIL_S_MUL_PREV2:
      condition = cmp(s, DXIL_FCMP_UNE, prior, fc(s, -FLT_MAX));
      condition = bin(s, DXIL_BINOP_AND, condition,
        cmp(s, DXIL_FCMP_OLE, unary(s, 6, prior), fc(s, FLT_MAX)));
      condition = bin(s, DXIL_BINOP_AND, condition,
        cmp(s, DXIL_FCMP_OLE, unary(s, 6, b), fc(s, FLT_MAX)));
      condition = bin(s, DXIL_BINOP_AND, condition, cmp(s, DXIL_FCMP_OGT, b, fc(s, 0.0f)));
      value = choose(s, condition, multiply(s, a, prior, 0), fc(s, -FLT_MAX));
      break;
    case XE_DXIL_S_MAX: case XE_DXIL_S_MIN: case XE_DXIL_S_MAXA: case XE_DXIL_S_MAXAF:
      if (opcode == XE_DXIL_S_MAXA || opcode == XE_DXIL_S_MAXAF)
        s->address = address_value(s, a, opcode == XE_DXIL_S_MAXA, 0.0f);
      value = identical ? a : choose(s,
        cmp(s, opcode == XE_DXIL_S_MIN ? DXIL_FCMP_OLT : DXIL_FCMP_OGE, a, b), a, b);
      break;
    case XE_DXIL_S_SEQ: case XE_DXIL_S_SGT: case XE_DXIL_S_SGE: case XE_DXIL_S_SNE:
      value = choose(s, cmp(s, comparisons[opcode - XE_DXIL_S_SEQ], a, fc(s, 0.0f)),
        fc(s, 1.0f), fc(s, 0.0f));
      break;
    case XE_DXIL_S_FRC: case XE_DXIL_S_TRUNC: case XE_DXIL_S_FLOOR:
    case XE_DXIL_S_SIN: case XE_DXIL_S_COS:
      intrinsic = opcode == XE_DXIL_S_FRC ? 22 : (opcode == XE_DXIL_S_TRUNC ? 29 :
        (opcode == XE_DXIL_S_FLOOR ? 27 : (opcode == XE_DXIL_S_SIN ? 13 : 12)));
      value = unary(s, intrinsic, a); break;
    case XE_DXIL_S_EXP: case XE_DXIL_S_LOG: case XE_DXIL_S_LOGC:
    case XE_DXIL_S_RCPC: case XE_DXIL_S_RCPF: case XE_DXIL_S_RCP:
    case XE_DXIL_S_RSQC: case XE_DXIL_S_RSQF: case XE_DXIL_S_RSQ:
    case XE_DXIL_S_SQRT:
      if (opcode >= XE_DXIL_S_RCPC && opcode <= XE_DXIL_S_RCP)
        value = bin(s, DXIL_BINOP_SDIV, fc(s, 1.0f), a);
      else {
        intrinsic = opcode == XE_DXIL_S_EXP ? 21 : (opcode == XE_DXIL_S_LOG || opcode == XE_DXIL_S_LOGC ?
          23 : (opcode == XE_DXIL_S_SQRT ? 24 : 25));
        value = unary(s, intrinsic, a);
      }
      value = approximate(s, value);
      if (opcode == XE_DXIL_S_LOGC || opcode == XE_DXIL_S_RCPC || opcode == XE_DXIL_S_RSQC) {
        negative_inf = xe_dxil_alu_float_bits(s, UINT32_C(0xff800000));
        value = choose(s, cmp(s, DXIL_FCMP_OEQ, value, negative_inf), fc(s, -FLT_MAX), value);
      }
      if (opcode == XE_DXIL_S_RCPC || opcode == XE_DXIL_S_RSQC) {
        positive_inf = xe_dxil_alu_float_bits(s, UINT32_C(0x7f800000));
        value = choose(s, cmp(s, DXIL_FCMP_OEQ, value, positive_inf), fc(s, FLT_MAX), value);
      }
      if (opcode == XE_DXIL_S_RCPF || opcode == XE_DXIL_S_RSQF) {
        positive_inf = xe_dxil_alu_float_bits(s, UINT32_C(0x7f800000));
        negative_inf = xe_dxil_alu_float_bits(s, UINT32_C(0xff800000));
        value = choose(s, cmp(s, DXIL_FCMP_OEQ, value, positive_inf), fc(s, 0.0f), value);
        value = choose(s, cmp(s, DXIL_FCMP_OEQ, value, negative_inf),
          xe_dxil_alu_float_bits(s, UINT32_C(0x80000000)), value);
      }
      break;
    case XE_DXIL_S_SETP_EQ: case XE_DXIL_S_SETP_NE: case XE_DXIL_S_SETP_GT: case XE_DXIL_S_SETP_GE:
      s->predicate = cmp(s, predicate_comparisons[opcode - XE_DXIL_S_SETP_EQ], a, fc(s, 0.0f));
      value = choose(s, s->predicate, fc(s, 0.0f), fc(s, 1.0f));
      break;
    case XE_DXIL_S_SETP_INV:
      s->predicate = cmp(s, DXIL_FCMP_OEQ, a, fc(s, 1.0f));
      value = choose(s, s->predicate, fc(s, 0.0f),
        choose(s, cmp(s, DXIL_FCMP_OEQ, a, fc(s, 0.0f)), fc(s, 1.0f), a));
      break;
    case XE_DXIL_S_SETP_POP:
      value = bin(s, DXIL_BINOP_SUB, a, fc(s, 1.0f));
      s->predicate = cmp(s, DXIL_FCMP_OLE, value, fc(s, 0.0f));
      value = choose(s, s->predicate, fc(s, 0.0f), value);
      break;
    case XE_DXIL_S_SETP_CLR:
      s->predicate = checked(s, dxil_module_get_int1_const(s->module, 0));
      value = fc(s, FLT_MAX); break;
    case XE_DXIL_S_SETP_RSTR:
      s->predicate = cmp(s, DXIL_FCMP_OEQ, a, fc(s, 0.0f));
      value = choose(s, s->predicate, fc(s, 0.0f), a); break;
    case XE_DXIL_S_KILL_EQ: case XE_DXIL_S_KILL_GT: case XE_DXIL_S_KILL_GE:
    case XE_DXIL_S_KILL_NE: case XE_DXIL_S_KILL_ONE:
      condition = cmp(s, opcode == XE_DXIL_S_KILL_ONE ? DXIL_FCMP_OEQ :
        comparisons[opcode - XE_DXIL_S_KILL_EQ], a, fc(s, opcode == XE_DXIL_S_KILL_ONE ? 1.0f : 0.0f));
      s->operation_kill = condition;
      s->killed = bin(s, DXIL_BINOP_OR, s->killed, condition);
      value = choose(s, condition, fc(s, 1.0f), fc(s, 0.0f)); break;
    case XE_DXIL_S_MULSC0: case XE_DXIL_S_MULSC1:
      value = mulsc(s, a, b, identical); break;
    case XE_DXIL_S_RETAIN_PREV: value = prior; break;
    default: s->failed = 1; return NULL;
  }
  s->previous_scalar = checked(s, value);
  return value;
}
