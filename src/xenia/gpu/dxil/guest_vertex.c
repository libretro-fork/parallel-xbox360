/* Xbox vertex format conversion. MSVC C89; BSD license, see LICENSE.
 * Copyright 2022 Ben Vanik. All rights reserved.
 * Format layout and normalization follow xenos.h and the guest translator. */
#include "guest_vertex.h"
#include <dxil_module.h>
#include <dxil_function.h>

typedef struct Format {
  unsigned count, floating;
  unsigned width[4], shift[4], word[4];
} Format;

static int layout(unsigned format, Format *f) {
  unsigned i;
  memset(f, 0, sizeof(*f));
  switch (format) {
  case 6: f->count = 4; for (i = 0; i < 4; ++i) { f->width[i] = 8; f->shift[i] = i * 8; } break;
  case 7: f->count = 4; for (i = 0; i < 3; ++i) { f->width[i] = 10; f->shift[i] = i * 10; }
    f->width[3] = 2; f->shift[3] = 30; break;
  case 16: f->count = 3; f->width[0] = 11; f->width[1] = 11; f->width[2] = 10;
    f->shift[1] = 11; f->shift[2] = 22; break;
  case 17: f->count = 3; f->width[0] = 10; f->width[1] = 11; f->width[2] = 11;
    f->shift[1] = 10; f->shift[2] = 21; break;
  case 25: case 26: case 31: case 32:
    f->count = format == 25 || format == 31 ? 2 : 4;
    f->floating = format >= 31;
    for (i = 0; i < f->count; ++i) { f->width[i] = 16; f->shift[i] = (i & 1) * 16; f->word[i] = i / 2; }
    break;
  case 33: case 34: case 35: case 36: case 37: case 38: case 57:
    f->count = format == 33 || format == 36 ? 1 : format == 34 || format == 37 ? 2 : format == 57 ? 3 : 4;
    f->floating = format >= 36;
    for (i = 0; i < f->count; ++i) { f->width[i] = 32; f->word[i] = i; }
    break;
  default: return 0;
  }
  return 1;
}

int xe_dxil_decode_vertex(const uint32_t *w, size_t count, XeDxilVertexInstruction *out) {
  Format f;
  if (!w || !out || count < 3 || (w[0] & 31)) return 0;
  memset(out, 0, sizeof(*out));
  out->source = (w[0] >> 5) & 63; out->source_relative = (w[0] >> 11) & 1;
  out->destination = (w[0] >> 12) & 63; out->destination_relative = (w[0] >> 18) & 1;
  out->constant_index = ((w[0] >> 20) & 31) * 3 + ((w[0] >> 25) & 3);
  out->source_component = w[0] >> 30;
  out->swizzle = w[1] & 4095; out->signed_value = (w[1] >> 12) & 1;
  out->integer_value = (w[1] >> 13) & 1; out->signed_no_zero = (w[1] >> 14) & 1;
  out->rounded_index = (w[1] >> 15) & 1; out->format = (w[1] >> 16) & 63;
  out->exponent_adjust = (int)((w[1] >> 24) & 63);
  if (out->exponent_adjust & 32) out->exponent_adjust -= 64;
  out->mini = (w[1] >> 30) & 1; out->predicated = w[1] >> 31;
  out->stride = w[2] & 255; out->offset = (int)((w[2] >> 8) & 8388607);
  if (out->offset & 4194304) out->offset -= 8388608;
  out->predicate_condition = w[2] >> 31;
  return (out->mini || out->constant_index < 96) && layout(out->format, &f) &&
    xe_dxil_vertex_needed_words(out->format, out->swizzle) >= 0;
}

int xe_dxil_vertex_needed_words(unsigned format, unsigned swizzle) {
  Format f;
  unsigned i, component, result = 0;
  if (swizzle > 4095 || !layout(format, &f)) return -1;
  for (i = 0; i < 4; ++i) {
    component = (swizzle >> (i * 3)) & 7;
    if (component == 6) return -1;
    if (component < f.count) result |= 1u << f.word[component];
  }
  return (int)result;
}

static XeDxilValue ic(XeDxilAlu *s, uint32_t n) {
  return dxil_module_get_int32_const(s->module, (int32_t)n);
}
static XeDxilValue fc(XeDxilAlu *s, float n) {
  return dxil_module_get_float_const(s->module, n);
}
static XeDxilValue bin(XeDxilAlu *s, enum dxil_bin_opcode op, XeDxilValue a, XeDxilValue b) {
  return a && b ? dxil_emit_binop(s->module, op, a, b, 0) : NULL;
}
static XeDxilValue compare(XeDxilAlu *s, enum dxil_cmp_pred op, XeDxilValue a, XeDxilValue b) {
  return a && b ? dxil_emit_cmp(s->module, op, a, b) : NULL;
}
static XeDxilValue swap(XeDxilAlu *s, XeDxilValue word, XeDxilValue endian) {
  XeDxilValue swapped, condition;
  swapped = bin(s, DXIL_BINOP_OR,
    bin(s, DXIL_BINOP_AND, bin(s, DXIL_BINOP_LSHR, word, ic(s, 8)), ic(s, 0x00ff00ff)),
    bin(s, DXIL_BINOP_SHL, bin(s, DXIL_BINOP_AND, word, ic(s, 0x00ff00ff)), ic(s, 8)));
  condition = compare(s, DXIL_ICMP_NE,
    bin(s, DXIL_BINOP_AND, bin(s, DXIL_BINOP_XOR, endian,
      bin(s, DXIL_BINOP_LSHR, endian, ic(s, 1))), ic(s, 1)), ic(s, 0));
  if (!swapped || !condition) return NULL;
  word = dxil_emit_select(s->module, condition, swapped, word);
  swapped = bin(s, DXIL_BINOP_OR, bin(s, DXIL_BINOP_LSHR, word, ic(s, 16)),
    bin(s, DXIL_BINOP_SHL, word, ic(s, 16)));
  condition = compare(s, DXIL_ICMP_NE,
    bin(s, DXIL_BINOP_AND, endian, ic(s, 2)), ic(s, 0));
  return swapped && condition ? dxil_emit_select(s->module, condition, swapped, word) : NULL;
}

static XeDxilValue unpack(XeDxilAlu *s, const XeDxilVertexInstruction *instruction,
                           const Format *f, unsigned component, XeDxilValue word) {
  unsigned width = f->width[component];
  XeDxilValue value, args[2], bound;
  const struct dxil_func *function;
  const struct dxil_type *float_type = dxil_module_get_float_type(s->module, 32);
  float denominator, scale;
  if (!word) return NULL;
  if (f->shift[component]) word = bin(s, DXIL_BINOP_LSHR, word, ic(s, f->shift[component]));
  if (width != 32) word = bin(s, DXIL_BINOP_AND, word, ic(s, (1u << width) - 1));
  if (!word) return NULL;
  if (f->floating) {
    if (width == 32) return dxil_emit_cast(s->module, DXIL_CAST_BITCAST, float_type, word);
    function = dxil_get_function(s->module, "dx.op.legacyF16ToF32", DXIL_NONE);
    args[0] = ic(s, 131); args[1] = word;
    return function && args[0] ? dxil_emit_call(s->module, function, args, 2) : NULL;
  }
  if (instruction->signed_value && width != 32) {
    word = bin(s, DXIL_BINOP_SHL, word, ic(s, 32 - width));
    word = bin(s, DXIL_BINOP_ASHR, word, ic(s, 32 - width));
  }
  if (!word) return NULL;
  value = dxil_emit_cast(s->module, instruction->signed_value ? DXIL_CAST_SITOFP : DXIL_CAST_UITOFP,
    float_type, word);
  if (!value || instruction->integer_value) return value;
  denominator = instruction->signed_value ?
    (width == 32 ? 2147483647.0f : (float)((1u << (width - 1)) - 1)) :
    (width == 32 ? 4294967295.0f : (float)((1u << width) - 1));
  if (instruction->signed_value && instruction->signed_no_zero) denominator += 0.5f;
  scale = 1.0f / denominator;
  value = bin(s, DXIL_BINOP_MUL, value, fc(s, scale));
  if (!value || !instruction->signed_value) return value;
  if (instruction->signed_no_zero) return bin(s, DXIL_BINOP_ADD, value, fc(s, 0.5f * scale));
  bound = fc(s, -1.0f);
  word = bound ? dxil_emit_cmp(s->module, DXIL_FCMP_OLT, value, bound) : NULL;
  return word ? dxil_emit_select(s->module, word, bound, value) : NULL;
}

int xe_dxil_vertex_convert(XeDxilAlu *s, const XeDxilVertexInstruction *instruction,
                            XeDxilValue words[4], XeDxilValue endian,
                            XeDxilValue values[4], unsigned *write_mask) {
  Format f;
  XeDxilValue swapped[4], components[4], bias;
  unsigned i, component, needed;
  if (!s || !instruction || !words || !endian || !values || !write_mask || s->failed) return 0;
  if (!s->module || !layout(instruction->format, &f) || instruction->swizzle > 4095 ||
      instruction->exponent_adjust < -32 || instruction->exponent_adjust > 31 ||
      !dxil_value_type_bitsize_equal_to(endian, 32)) goto fail;
  needed = (unsigned)xe_dxil_vertex_needed_words(instruction->format, instruction->swizzle);
  if (needed == (unsigned)-1) goto fail;
  for (i = 0; i < 4; ++i) { swapped[i] = NULL; components[i] = NULL; }
  for (i = 0; i < 4; ++i) if (needed & (1u << i)) {
    if (!words[i] || !dxil_value_type_bitsize_equal_to(words[i], 32)) goto fail;
    swapped[i] = swap(s, words[i], endian);
    if (!swapped[i]) goto fail;
  }
  bias = xe_dxil_alu_float_bits(s, (uint32_t)(127 + instruction->exponent_adjust) << 23);
  if (!bias) goto fail;
  *write_mask = 0;
  for (i = 0; i < 4; ++i) {
    values[i] = NULL; component = (instruction->swizzle >> (i * 3)) & 7;
    if (component >= 6) continue;
    *write_mask |= 1u << i;
    if (component >= f.count) { values[i] = fc(s, component == 5 ? 1.0f : 0.0f); }
    else {
      if (!components[component]) {
        components[component] = unpack(s, instruction, &f, component, swapped[f.word[component]]);
        if (instruction->exponent_adjust) components[component] = bin(s, DXIL_BINOP_MUL, components[component], bias);
      }
      values[i] = components[component];
    }
    if (!values[i]) goto fail;
  }
  return !s->failed;
fail:
  s->failed = 1;
  return 0;
}

static int begin(XeDxilAlu *s, const XeDxilAluIo *io, unsigned block) {
  struct dxil_func_def *function = s->module->cur_emitting_func;
  if (block >= function->num_basic_block_ids || function->basic_block_ids[block] != -1) return 0;
  function->basic_block_ids[block] = (int)function->curr_block;
  *io->current_block = block;
  return 1;
}

static int save(XeDxilAlu *s, XeDxilValue pointer, XeDxilValue value) {
  return pointer && value && dxil_emit_store(s->module, value, pointer, 4, 0);
}

static XeDxilValue load(XeDxilAlu *s, XeDxilValue pointer) {
  return pointer ? dxil_emit_load(s->module, pointer, 4, 0) : NULL;
}

int xe_dxil_vertex_fetch_init(XeDxilAlu *s, XeDxilVertexFetch *fetch) {
  const struct dxil_type *type;
  XeDxilValue one, zero;
  if (!s || !s->module || s->failed || !fetch) return 0;
  if (!s->module->cur_emitting_func || s->module->cur_emitting_func->curr_block) goto fail;
  type = dxil_module_get_int_type(s->module, 32); one = ic(s, 1); zero = ic(s, 0);
  if (!type || !one || !zero) goto fail;
  fetch->address_storage = dxil_emit_alloca(s->module, type, one, 4);
  fetch->bound_storage = dxil_emit_alloca(s->module, type, one, 4);
  fetch->endian_storage = dxil_emit_alloca(s->module, type, one, 4);
  if (save(s, fetch->address_storage, zero) && save(s, fetch->bound_storage, zero) &&
      save(s, fetch->endian_storage, zero)) return 1;
fail:
  s->failed = 1;
  return 0;
}

int xe_dxil_emit_vertex_fetch(XeDxilAlu *s, const XeDxilVertexInstruction *instruction,
                               const XeDxilAluIo *io, XeDxilVertexFetch *fetch) {
  XeDxilValue base, address, bound, endian, first, second, index, args[2];
  XeDxilValue words[4], values[4], condition, pointer, offset;
  const struct dxil_func *function;
  unsigned blocks = 0, mask, i;
  int needed;
  if (!s || !s->module || s->failed || !instruction || !io || !fetch) return 0;
  needed = xe_dxil_vertex_needed_words(instruction->format, instruction->swizzle);
  if (!s->module->cur_emitting_func || needed < 0 || !io->write || !io->current_block || !fetch->address_storage ||
      !fetch->bound_storage || !fetch->endian_storage ||
      (!instruction->mini && (!fetch->constant || instruction->constant_index >= 96 ||
       instruction->stride > 255 || (instruction->stride && !io->read))) ||
      (needed && !fetch->load) || instruction->source > 63 || instruction->source_component > 3 ||
      instruction->destination > 63 || instruction->offset < -4194304 || instruction->offset > 4194303 ||
      instruction->exponent_adjust < -32 || instruction->exponent_adjust > 31 ||
      (((!instruction->mini && instruction->stride && instruction->source_relative) ||
        instruction->destination_relative) && !io->loop_address)) goto fail;
  if (instruction->predicated) {
    condition = s->predicate;
    if (!condition) goto fail;
    if (!instruction->predicate_condition) condition = bin(s, DXIL_BINOP_XOR, condition,
      dxil_module_get_int1_const(s->module, 1));
    if (!condition || !dxil_function_reserve_blocks(s->module, 2, &blocks) ||
        !dxil_emit_branch(s->module, condition, blocks, blocks + 1) || !begin(s, io, blocks)) goto fail;
  }
  if (instruction->mini) {
    address = load(s, fetch->address_storage); bound = load(s, fetch->bound_storage);
    endian = load(s, fetch->endian_storage);
  } else {
    first = fetch->constant(fetch->context, instruction->constant_index, 0);
    second = fetch->constant(fetch->context, instruction->constant_index, 1);
    if (!first || !second || !dxil_value_type_bitsize_equal_to(first, 32) ||
        !dxil_value_type_bitsize_equal_to(second, 32)) goto fail;
    base = bin(s, DXIL_BINOP_AND, bin(s, DXIL_BINOP_LSHR, first, ic(s, 2)), ic(s, 0x07ffffff));
    bound = bin(s, DXIL_BINOP_ADD, base,
      bin(s, DXIL_BINOP_AND, bin(s, DXIL_BINOP_LSHR, second, ic(s, 2)), ic(s, 0x00ffffff)));
    endian = bin(s, DXIL_BINOP_AND, second, ic(s, 3)); address = base;
    if (instruction->stride) {
      index = io->read(io->context, 0, instruction->source, instruction->source_component,
        instruction->source_relative ? io->loop_address : NULL);
      if (instruction->rounded_index) index = bin(s, DXIL_BINOP_ADD, index, fc(s, 0.5f));
      args[0] = ic(s, 27); args[1] = index;
      function = dxil_get_function(s->module, "dx.op.unary", DXIL_F32);
      if (!function || !args[0] || !index) goto fail;
      index = dxil_emit_call(s->module, function, args, 2);
      if (!index) goto fail;
      index = dxil_emit_cast(s->module, DXIL_CAST_FPTOSI, dxil_module_get_int_type(s->module, 32), index);
      address = bin(s, DXIL_BINOP_ADD, base, bin(s, DXIL_BINOP_MUL, index, ic(s, instruction->stride)));
    }
    if (!save(s, fetch->address_storage, address) || !save(s, fetch->bound_storage, bound) ||
        !save(s, fetch->endian_storage, endian)) goto fail;
  }
  if (!address || !bound || !endian) goto fail;
  for (i = 0; i < 4; ++i) words[i] = NULL;
  for (i = 0; i < 4; ++i) if (needed & (1u << i)) {
    pointer = bin(s, DXIL_BINOP_ADD, address, ic(s, (uint32_t)instruction->offset + i));
    condition = compare(s, DXIL_ICMP_ULT, pointer, bound);
    if (!pointer || !condition) goto fail;
    words[i] = fetch->load(fetch->context, pointer, condition);
    if (!words[i]) goto fail;
    /* Also mask the result, so byte order and normalization see zero. */
    words[i] = dxil_emit_select(s->module, condition, words[i], ic(s, 0));
    if (!words[i]) goto fail;
  }
  if (!xe_dxil_vertex_convert(s, instruction, words, endian, values, &mask)) goto fail;
  offset = instruction->destination_relative ? io->loop_address : NULL;
  if (mask && !io->write(io->context, 0, instruction->destination, mask, values, offset)) goto fail;
  if (instruction->predicated && (!dxil_emit_branch(s->module, NULL, blocks + 1, 0) ||
      !begin(s, io, blocks + 1))) goto fail;
  return !s->failed;
fail:
  s->failed = 1;
  return 0;
}
