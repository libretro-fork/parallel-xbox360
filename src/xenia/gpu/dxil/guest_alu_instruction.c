/* Direct Xbox 360 ALU instruction frontend. MSVC C89; BSD, see LICENSE.
 * Copyright 2015 Ben Vanik. All rights reserved.
 * Encoding and co-issue rules follow ucode.h and shader_translator.cc. */
#include "guest_alu_instruction.h"
#include <dxil_module.h>

static unsigned vector_count(unsigned opcode) {
  if (opcode == 8 || opcode == 9 || opcode == 10 || opcode == 19) return 1;
  if (opcode == 11 || (opcode >= 12 && opcode <= 14) || opcode == 17) return 3;
  return opcode < 30 ? 2 : 0;
}

static void operand(const uint32_t *w, unsigned number, XeDxilOperand *out) {
  unsigned shift = (2 - number) * 8;
  unsigned reg = (w[2] >> shift) & 255;
  unsigned temporary = (w[2] >> (31 - number)) & 1;
  unsigned swizzle = (w[1] >> shift) & 255;
  unsigned previous_constant = number && !(w[2] & UINT32_C(0x80000000));
  unsigned relative;
  unsigned i;
  if (number == 2 && !(w[2] & UINT32_C(0x40000000))) previous_constant = 1;
  memset(out, 0, sizeof(*out));
  out->is_constant = !temporary;
  out->index = temporary ? reg & 63 : reg;
  out->absolute_value = temporary ? (reg >> 7) & 1 : (w[0] >> 7) & 1;
  out->negate = (w[1] >> (26 - number)) & 1;
  if (temporary) out->addressing = reg & 64 ? XE_DXIL_LOOP_RELATIVE : XE_DXIL_ABSOLUTE;
  else {
    relative = (w[1] >> (previous_constant ? 30 : 31)) & 1;
    out->addressing = !relative ? XE_DXIL_ABSOLUTE :
      (w[1] & UINT32_C(0x20000000) ? XE_DXIL_ADDRESS_RELATIVE : XE_DXIL_LOOP_RELATIVE);
  }
  for (i = 0; i < 4; ++i) out->components[i] = ((swizzle >> (i * 2)) + i) & 3;
}

int xe_dxil_decode_alu(const uint32_t *w, size_t count, XeDxilAluInstruction *out) {
  unsigned vector_mask;
  unsigned scalar_mask;
  unsigned i;
  unsigned swizzle;
  unsigned addressed;
  if (!w || !out || count < 3) return 0;
  memset(out, 0, sizeof(*out));
  out->vector_opcode = (w[2] >> 24) & 31;
  out->scalar_opcode = w[0] >> 26;
  out->is_export = (w[0] >> 15) & 1;
  out->is_predicated = (w[1] >> 28) & 1;
  out->predicate_condition = (w[1] >> 27) & 1;
  out->vector_destination = w[0] & 63;
  out->scalar_destination = out->is_export ? out->vector_destination : (w[0] >> 8) & 63;
  out->vector_relative = !out->is_export && ((w[0] >> 6) & 1);
  out->scalar_relative = !out->is_export && ((w[0] >> 14) & 1);
  out->vector_clamp = (w[0] >> 24) & 1;
  out->scalar_clamp = (w[0] >> 25) & 1;
  vector_mask = (w[0] >> 16) & 15; scalar_mask = (w[0] >> 20) & 15;
  out->vector_mask = vector_mask; out->scalar_mask = scalar_mask;
  if (out->is_export) {
    out->vector_mask &= ~scalar_mask;
    out->scalar_mask &= ~vector_mask;
    out->one_mask = vector_mask & scalar_mask;
    out->zero_mask = w[0] & UINT32_C(0x4000) ? 15 & ~(vector_mask | scalar_mask) : 0;
  }
  if (out->scalar_opcode > 50 || out->scalar_opcode == 41 ||
      (out->vector_opcode >= 30 && out->vector_mask)) return 0;
  out->vector_operand_count = vector_count(out->vector_opcode);
  for (i = 0; i < out->vector_operand_count; ++i) operand(w, i, &out->vector_operands[i]);
  if (out->scalar_opcode == 33 || out->scalar_opcode == 50) return 1;
  out->scalar_operand_count = out->scalar_opcode >= 42 && out->scalar_opcode <= 47 ? 2 : 1;
  operand(w, 2, &out->scalar_operands[0]);
  swizzle = w[1] & 255;
  out->scalar_operands[0].components[0] = ((swizzle >> 6) + 3) & 3;
  out->scalar_operands[0].components[1] = out->vector_operand_count == 3 ? ((swizzle >> 4) + 2) & 3 : swizzle & 3;
  if (out->scalar_operand_count == 2) {
    out->scalar_operands[1] = out->scalar_operands[0];
    out->scalar_operands[1].is_constant = 0;
    out->scalar_operands[1].index = (out->scalar_opcode & 1) | (((w[2] >> 29) & 1) << 1) | (swizzle & 60);
    out->scalar_operands[1].addressing = XE_DXIL_ABSOLUTE;
    out->scalar_operands[1].components[0] = swizzle & 3;
    out->scalar_operands[0].is_constant = 1;
    out->scalar_operands[0].index = w[2] & 255;
    addressed = (w[1] >> ((w[2] & UINT32_C(0xc0000000)) == UINT32_C(0xc0000000) ? 31 : 30)) & 1;
    out->scalar_operands[0].addressing = !addressed ? XE_DXIL_ABSOLUTE :
      (w[1] & UINT32_C(0x20000000) ? XE_DXIL_ADDRESS_RELATIVE : XE_DXIL_LOOP_RELATIVE);
    out->scalar_operands[0].absolute_value = (w[0] >> 7) & 1;
    out->scalar_operands[1].absolute_value = out->scalar_operands[0].absolute_value;
  }
  return 1;
}

static XeDxilValue load(XeDxilAlu *s, const XeDxilAluIo *io,
                         const XeDxilOperand *operand_value, unsigned component) {
  XeDxilValue offset = NULL;
  XeDxilValue value;
  if (operand_value->addressing == XE_DXIL_LOOP_RELATIVE) offset = io->loop_address;
  if (operand_value->addressing == XE_DXIL_ADDRESS_RELATIVE) offset = s->address;
  if (operand_value->addressing && !offset) { s->failed = 1; return NULL; }
  value = io->read(io->context, operand_value->is_constant, operand_value->index,
    operand_value->components[component], offset);
  if (!value) { s->failed = 1; return NULL; }
  return xe_dxil_alu_modify(s, value, operand_value->absolute_value, operand_value->negate);
}

static int same(const XeDxilOperand *a, unsigned ac, const XeDxilOperand *b, unsigned bc) {
  return a->is_constant == b->is_constant && a->index == b->index &&
    a->addressing == b->addressing && a->absolute_value == b->absolute_value &&
    a->negate == b->negate && a->components[ac] == b->components[bc];
}

static unsigned needed(unsigned opcode, unsigned operand_index, unsigned mask) {
  if (opcode == 15 || opcode == 19) return mask ? 15 : 0;
  if (opcode == 16) return mask ? 7 : 0;
  if (opcode == 17) return mask ? (operand_index == 2 ? 1 : 3) : 0;
  if (opcode == 18) return mask && !operand_index ? 13 : 0;
  if (opcode >= 20 && opcode <= 23) return mask ? 9 : 8;
  if (opcode >= 24 && opcode <= 27) return 15;
  if (opcode == 28) return mask & (operand_index ? 10 : 6);
  if (opcode == 29 && !operand_index) return mask | 8;
  return mask;
}

static XeDxilValue merge_value(XeDxilAlu *s, XeDxilValue old_value,
                                XeDxilValue new_value, unsigned entry, unsigned body) {
  struct dxil_instr *phi;
  XeDxilValue incoming[2];
  unsigned blocks[2];
  if (old_value == new_value) return old_value;
  incoming[0] = old_value; incoming[1] = new_value;
  blocks[0] = entry; blocks[1] = body;
  phi = dxil_emit_phi(s->module, dxil_value_get_type(new_value));
  if (!phi || !dxil_phi_add_incoming(phi, incoming, blocks, 2)) { s->failed = 1; return NULL; }
  return dxil_instr_get_return_value(phi);
}

int xe_dxil_emit_alu_instruction(XeDxilAlu *s, const XeDxilAluInstruction *instruction,
                                 const XeDxilAluIo *io, unsigned body, unsigned merge) {
  XeDxilValue operands[3][4];
  XeDxilValue vector_result[4];
  XeDxilValue scalar_values[4];
  XeDxilValue scalar_a = NULL;
  XeDxilValue scalar_b = NULL;
  XeDxilValue scalar_result;
  XeDxilValue condition;
  XeDxilValue offset;
  XeDxilValue zero;
  XeDxilValue one;
  XeDxilAlu old;
  struct dxil_func_def *function;
  unsigned entry = 0;
  unsigned body_exit;
  unsigned i;
  unsigned j;
  unsigned mask;
  unsigned identical = 0;
  unsigned opcode;
  int vector_active;
  int scalar_two;
  if (!s) return 0;
  if (s->failed || !instruction || !io || !io->read || !io->write) {
    s->failed = 1; return 0;
  }
  old = *s;
  function = s->module->cur_emitting_func;
  if (instruction->is_predicated) {
    if (!io->current_block || !function) goto fail;
    entry = *io->current_block;
    if (entry >= function->num_basic_block_ids || body >= function->num_basic_block_ids ||
        merge >= function->num_basic_block_ids || entry == body || entry == merge || body == merge ||
        function->basic_block_ids[entry] != (int)function->curr_block ||
        function->basic_block_ids[body] != -1 || function->basic_block_ids[merge] != -1) goto fail;
    condition = s->predicate;
    if (!instruction->predicate_condition) condition = dxil_emit_binop(s->module, DXIL_BINOP_XOR,
      condition, dxil_module_get_int1_const(s->module, 1), 0);
    if (!condition || !dxil_emit_branch(s->module, condition, body, merge)) goto fail;
    function->basic_block_ids[body] = (int)function->curr_block;
    *io->current_block = body;
  }
  zero = dxil_module_get_float_const(s->module, 0.0f);
  one = dxil_module_get_float_const(s->module, 1.0f);
  if (!zero || !one) goto fail;
  for (i = 0; i < 3; ++i) for (j = 0; j < 4; ++j) operands[i][j] = zero;
  for (j = 0; j < 4; ++j) vector_result[j] = zero;
  opcode = instruction->vector_opcode;
  vector_active = instruction->vector_mask || (opcode >= 20 && opcode <= 27) || opcode == 29;
  if (vector_active) {
    for (i = 0; i < instruction->vector_operand_count; ++i) {
      mask = needed(opcode, i, instruction->vector_mask);
      for (j = 0; j < 4; ++j) if (mask & (1u << j)) {
        operands[i][j] = load(s, io, &instruction->vector_operands[i], j);
        if (!operands[i][j]) goto fail;
      }
    }
    if (instruction->vector_operand_count >= 2) for (j = 0; j < 4; ++j)
      if (same(&instruction->vector_operands[0], j, &instruction->vector_operands[1], j)) identical |= 1u << j;
    if (!xe_dxil_alu_vector(s, opcode, operands, identical, vector_result)) goto fail;
    if (opcode >= 24 && opcode <= 27 &&
        (!io->kill || !io->kill(io->context, s->operation_kill))) goto fail;
  }
  opcode = instruction->scalar_opcode;
  if (instruction->scalar_operand_count) {
    scalar_a = load(s, io, &instruction->scalar_operands[0], 0);
    if (!scalar_a) goto fail;
    if (instruction->scalar_operand_count == 2) scalar_b = load(s, io, &instruction->scalar_operands[1], 0);
    else {
      scalar_two = opcode == 0 || opcode == 2 || opcode == 4 || opcode == 5 || opcode == 6 ||
        opcode == 23 || opcode == 24 || opcode == 25;
      if (scalar_two) scalar_b = load(s, io, &instruction->scalar_operands[0], 1);
    }
  }
  identical = instruction->scalar_operand_count == 1 &&
    instruction->scalar_operands[0].components[0] == instruction->scalar_operands[0].components[1];
  scalar_result = xe_dxil_alu_scalar(s, opcode, scalar_a, scalar_b, identical != 0);
  if (!scalar_result || s->failed) goto fail;
  if (opcode >= 35 && opcode <= 39 &&
      (!io->kill || !io->kill(io->context, s->operation_kill))) goto fail;
  mask = instruction->vector_mask | instruction->zero_mask | instruction->one_mask;
  if (mask) {
    for (j = 0; j < 4; ++j) {
      if (instruction->zero_mask & (1u << j)) vector_result[j] = zero;
      else if (instruction->one_mask & (1u << j)) vector_result[j] = one;
      else if ((instruction->vector_mask & (1u << j)) && instruction->vector_clamp)
        vector_result[j] = xe_dxil_alu_saturate(s, vector_result[j]);
    }
    offset = instruction->vector_relative ? io->loop_address : NULL;
    if ((instruction->vector_relative && !offset) || s->failed || !io->write(io->context,
      instruction->is_export, instruction->vector_destination, mask, vector_result, offset)) goto fail;
  }
  if (instruction->scalar_mask) {
    if (instruction->scalar_clamp) scalar_result = xe_dxil_alu_saturate(s, scalar_result);
    for (j = 0; j < 4; ++j) scalar_values[j] = scalar_result;
    offset = instruction->scalar_relative ? io->loop_address : NULL;
    if ((instruction->scalar_relative && !offset) || s->failed || !io->write(io->context,
      instruction->is_export, instruction->scalar_destination, instruction->scalar_mask, scalar_values, offset)) goto fail;
  }
  if (instruction->is_predicated) {
    body_exit = *io->current_block;
    if (body_exit >= function->num_basic_block_ids ||
        function->curr_block != (unsigned)function->basic_block_ids[body_exit] ||
        !dxil_emit_branch(s->module, NULL, merge, 0)) goto fail;
    function->basic_block_ids[merge] = (int)function->curr_block;
    s->previous_scalar = merge_value(s, old.previous_scalar, s->previous_scalar, entry, body_exit);
    s->predicate = merge_value(s, old.predicate, s->predicate, entry, body_exit);
    s->address = merge_value(s, old.address, s->address, entry, body_exit);
    s->killed = merge_value(s, old.killed, s->killed, entry, body_exit);
    s->operation_kill = merge_value(s, old.operation_kill, s->operation_kill, entry, body_exit);
    *io->current_block = merge;
  }
  return !s->failed;
fail:
  s->failed = 1;
  return 0;
}
