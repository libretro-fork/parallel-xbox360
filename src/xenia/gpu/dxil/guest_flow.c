/* Xbox shader control-flow lowering. MSVC C89; BSD license, see LICENSE.
 * Copyright 2015-2022 Ben Vanik. All rights reserved.
 * Encoding and loop stack behavior follow ucode.h and the guest translator. */
#include "guest_flow.h"
#include <dxil_module.h>

typedef struct Control {
  uint32_t low;
  unsigned high;
  unsigned opcode;
} Control;

enum { FLOW_DISPATCH = 1, FLOW_LOOPS = 2, FLOW_CALLS = 4 };

typedef struct Flow {
  const XeDxilGuestFlow *config;
  struct dxil_module *module;
  unsigned base;
  unsigned exit;
  unsigned dispatch;
  unsigned features;
  uint32_t labels[256];
  XeDxilValue program_counter;
  XeDxilValue state[5];
  XeDxilValue loop_count[4];
  XeDxilValue loop_address[4];
  XeDxilValue return_address[4];
  XeDxilValue call_depth;
} Flow;

static void control(const Flow *f, unsigned index, Control *out) {
  const uint32_t *p = f->config->words + (index / 2) * 3;
  out->low = index & 1 ? (p[1] >> 16) | (p[2] << 16) : p[0];
  out->high = index & 1 ? p[2] >> 16 : p[1] & 65535;
  out->opcode = out->high >> 12;
}

static int exec_opcode(unsigned opcode) {
  return (opcode >= 1 && opcode <= 6) || opcode == 13 || opcode == 14;
}

static XeDxilValue integer(Flow *f, uint32_t value) {
  return dxil_module_get_int32_const(f->module, (int32_t)value);
}

static XeDxilValue binary(Flow *f, enum dxil_bin_opcode opcode, XeDxilValue a, XeDxilValue b) {
  return a && b ? dxil_emit_binop(f->module, opcode, a, b, 0) : NULL;
}

static XeDxilValue compare(Flow *f, enum dxil_cmp_pred opcode, XeDxilValue a, XeDxilValue b) {
  return a && b ? dxil_emit_cmp(f->module, opcode, a, b) : NULL;
}

static XeDxilValue load(Flow *f, XeDxilValue pointer) {
  return pointer ? dxil_emit_load(f->module, pointer, 4, 0) : NULL;
}

static int store(Flow *f, XeDxilValue pointer, XeDxilValue value) {
  return pointer && value && dxil_emit_store(f->module, value, pointer, 4, 0);
}

static int new_block(Flow *f, unsigned *id) {
  return dxil_function_reserve_blocks(f->module, 1, id);
}

static int begin(Flow *f, unsigned id) {
  struct dxil_func_def *function = f->module->cur_emitting_func;
  if (id >= function->num_basic_block_ids || function->basic_block_ids[id] != -1) return 0;
  function->basic_block_ids[id] = (int)function->curr_block;
  *f->config->io->current_block = id;
  return 1;
}

static unsigned target(const Flow *f, unsigned index) {
  return index == f->config->control_flow_count ? f->exit : f->base + index;
}

static int branch(Flow *f, unsigned id) {
  return dxil_emit_branch(f->module, NULL, id, 0);
}

/* Backward edges, loops and calls go through one dominating loop header.
 * Direct forward edges form a DAG between entries, preserving reducibility. */
static int jump(Flow *f, unsigned address) {
  if (address == f->config->control_flow_count) return branch(f, f->exit);
  return store(f, f->program_counter, integer(f, address)) && branch(f, f->dispatch);
}

static int save_state(Flow *f) {
  XeDxilAlu *s = f->config->alu;
  return store(f, f->state[0], s->previous_scalar) && store(f, f->state[1], s->predicate) &&
    store(f, f->state[2], s->address) && store(f, f->state[3], s->killed) &&
    store(f, f->state[4], s->operation_kill);
}

static int load_state(Flow *f) {
  XeDxilAlu *s = f->config->alu;
  s->previous_scalar = load(f, f->state[0]); s->predicate = load(f, f->state[1]);
  s->address = load(f, f->state[2]); s->killed = load(f, f->state[3]);
  s->operation_kill = load(f, f->state[4]);
  f->config->io->loop_address = (f->features & FLOW_LOOPS) ?
    load(f, f->loop_address[0]) : integer(f, 0);
  return s->previous_scalar && s->predicate && s->address && s->killed &&
    s->operation_kill && f->config->io->loop_address;
}

static XeDxilValue condition(Flow *f, const Control *c, int predicate, unsigned index) {
  XeDxilValue value;
  if (predicate) value = f->config->alu->predicate;
  else value = f->config->boolean_constant ?
    f->config->boolean_constant(f->config->context, index) : NULL;
  if (!value || !dxil_value_type_bitsize_equal_to(value, 1)) return NULL;
  if (!(c->high & 1024)) value = binary(f, DXIL_BINOP_XOR, value,
    dxil_module_get_int1_const(f->module, 1));
  return value;
}

static XeDxilValue loop_constant(Flow *f, unsigned index) {
  XeDxilValue value = f->config->loop_constant ?
    f->config->loop_constant(f->config->context, index) : NULL;
  return value && dxil_value_type_bitsize_equal_to(value, 32) ? value : NULL;
}

static int push(Flow *f, XeDxilValue stack[4], XeDxilValue value) {
  unsigned i;
  if (!value) return 0;
  for (i = 3; i; --i) if (!store(f, stack[i], load(f, stack[i - 1]))) return 0;
  return store(f, stack[0], value);
}

static int pop(Flow *f, XeDxilValue stack[4]) {
  unsigned i;
  for (i = 0; i < 3; ++i) if (!store(f, stack[i], load(f, stack[i + 1]))) return 0;
  return store(f, stack[3], integer(f, 0));
}

static int emit_exec(Flow *f, const Control *c, unsigned index) {
  XeDxilAluInstruction instruction;
  XeDxilValue test;
  unsigned body;
  unsigned merge;
  unsigned address = c->low & 4095;
  unsigned count = (c->low >> 12) & 7;
  unsigned sequence = (c->low >> 16) & 4095;
  unsigned i;
  unsigned is_end = c->opcode == 2 || c->opcode == 4 || c->opcode == 6 || c->opcode == 14;
  const uint32_t *words;
  if (c->opcode != 1 && c->opcode != 2) {
    test = condition(f, c, c->opcode == 5 || c->opcode == 6, (c->high >> 2) & 255);
    if (!test || !save_state(f) || !new_block(f, &body) ||
        !dxil_emit_branch(f->module, test, body, target(f, index + 1)) || !begin(f, body)) return 0;
  }
  for (i = 0; i < count; ++i) {
    words = f->config->words + ((size_t)address + i) * 3;
    if (sequence & (1u << (i * 2))) {
      if (!f->config->fetch || !f->config->fetch(f->config->context, words,
        (sequence >> (i * 2 + 1)) & 1, f->config->alu, f->config->io)) return 0;
    } else {
      if (!xe_dxil_decode_alu(words, 3, &instruction)) return 0;
      body = 0; merge = 0;
      if (instruction.is_predicated && (!new_block(f, &body) || !new_block(f, &merge))) return 0;
      if (!xe_dxil_emit_alu_instruction(f->config->alu, &instruction, f->config->io, body, merge)) return 0;
    }
  }
  return save_state(f) && branch(f, is_end ? f->exit : target(f, index + 1));
}

static int emit_loop_start(Flow *f, const Control *c, unsigned index) {
  XeDxilValue packed = loop_constant(f, (c->low >> 16) & 31);
  XeDxilValue count;
  XeDxilValue address;
  XeDxilValue zero;
  unsigned skip;
  if (!packed) return 0;
  count = binary(f, DXIL_BINOP_AND, packed, integer(f, 255));
  address = c->low & 8192 ? load(f, f->loop_address[0]) :
    binary(f, DXIL_BINOP_AND, binary(f, DXIL_BINOP_LSHR, packed, integer(f, 8)), integer(f, 255));
  if (!push(f, f->loop_count, count) || !push(f, f->loop_address, address) || !save_state(f)) return 0;
  zero = compare(f, DXIL_ICMP_EQ, count, integer(f, 0));
  return zero && new_block(f, &skip) &&
    dxil_emit_branch(f->module, zero, skip, target(f, index + 1)) &&
    begin(f, skip) && jump(f, c->low & 8191);
}

static int emit_loop_end(Flow *f, const Control *c, unsigned index) {
  XeDxilValue count = binary(f, DXIL_BINOP_SUB, load(f, f->loop_count[0]), integer(f, 1));
  XeDxilValue stop = compare(f, DXIL_ICMP_EQ, count, integer(f, 0));
  XeDxilValue packed;
  XeDxilValue increment;
  XeDxilValue test;
  unsigned done;
  unsigned again;
  if (c->low & UINT32_C(0x200000)) {
    test = condition(f, c, 1, 0);
    stop = binary(f, DXIL_BINOP_OR, stop, test);
  }
  if (!stop || !save_state(f) || !new_block(f, &done) || !new_block(f, &again) ||
      !dxil_emit_branch(f->module, stop, done, again) || !begin(f, done) ||
      !pop(f, f->loop_count) || !pop(f, f->loop_address) || !branch(f, target(f, index + 1)) ||
      !begin(f, again)) return 0;
  packed = loop_constant(f, (c->low >> 16) & 31);
  increment = binary(f, DXIL_BINOP_ASHR,
    binary(f, DXIL_BINOP_SHL, packed, integer(f, 8)), integer(f, 24));
  return store(f, f->loop_count[0], count) && store(f, f->loop_address[0],
    binary(f, DXIL_BINOP_ADD, load(f, f->loop_address[0]), increment)) && jump(f, c->low & 8191);
}

static int emit_call_or_jump(Flow *f, const Control *c, unsigned index) {
  XeDxilValue test;
  XeDxilValue depth;
  XeDxilValue increased;
  XeDxilValue below_limit;
  unsigned taken;
  if (!save_state(f)) return 0;
  /* Forward edges cannot form a cycle between dispatcher entries. Keep the
   * dispatcher for backward/self edges, loops and calls; commit ALU state
   * before either successor reloads it. A next-instruction jump is a no-op. */
  if (c->opcode == 11 && (c->low & 8191) > index) {
    if ((c->low & 8191) == index + 1 || (c->low & 8192))
      return branch(f, target(f, c->low & 8191));
    test = condition(f, c, (c->low >> 14) & 1, (c->high >> 2) & 255);
    return test && dxil_emit_branch(f->module, test,
      target(f, c->low & 8191), target(f, index + 1));
  }
  if (!(c->low & 8192)) {
    test = condition(f, c, (c->low >> 14) & 1, (c->high >> 2) & 255);
    if (!test) return 0;
    if (!new_block(f, &taken) || !dxil_emit_branch(f->module, test, taken, target(f, index + 1)) ||
        !begin(f, taken)) return 0;
  }
  if (c->opcode == 11) return jump(f, c->low & 8191);
  depth = load(f, f->call_depth);
  increased = binary(f, DXIL_BINOP_ADD, depth, integer(f, 1));
  below_limit = compare(f, DXIL_ICMP_ULT, depth, integer(f, 4));
  if (!depth || !increased || !below_limit || !new_block(f, &taken) ||
      !dxil_emit_branch(f->module, below_limit, taken, target(f, index + 1)) ||
      !begin(f, taken)) return 0;
  /* Like the interpreter: an exhausted four-entry stack skips the call,
   * without shifting away a live return address. */
  return store(f, f->call_depth, increased) &&
    push(f, f->return_address, integer(f, index + 1)) && jump(f, c->low & 8191);
}

static int emit_return(Flow *f, unsigned index) {
  XeDxilValue depth;
  XeDxilValue has_call;
  XeDxilValue address;
  unsigned taken;
  if (!(f->features & FLOW_CALLS))
    return save_state(f) && branch(f, target(f, index + 1));
  depth = load(f, f->call_depth);
  has_call = compare(f, DXIL_ICMP_NE, depth, integer(f, 0));
  if (!has_call || !save_state(f) || !new_block(f, &taken) ||
      !dxil_emit_branch(f->module, has_call, taken, target(f, index + 1)) || !begin(f, taken)) return 0;
  address = load(f, f->return_address[0]);
  if (!address || !pop(f, f->return_address) || !store(f, f->call_depth,
      binary(f, DXIL_BINOP_SUB, depth, integer(f, 1)))) return 0;
  return store(f, f->program_counter, address) && branch(f, f->dispatch);
}

static int validate(Flow *f) {
  Control c;
  unsigned i;
  unsigned address;
  unsigned count;
  size_t triples = f->config->word_count / 3;
  f->labels[0] = 1;
  for (i = 0; i < f->config->control_flow_count; ++i) {
    control(f, i, &c);
    if (exec_opcode(c.opcode)) {
      address = c.low & 4095; count = (c.low >> 12) & 7;
      if (count && (address > triples || count > triples - address)) return 0;
      if (c.opcode != 1 && c.opcode != 2 && c.opcode != 5 && c.opcode != 6 &&
          !f->config->boolean_constant) return 0;
      if (count && !f->config->fetch) {
        unsigned slot;
        for (slot = 0; slot < count; ++slot)
          if (((c.low >> 16) & 4095) & (1u << (slot * 2))) return 0;
      }
      if (f->config->prepare_export) {
        unsigned slot;
        for (slot = 0; slot < count; ++slot) {
          const uint32_t *instruction;
          if (((c.low >> 16) & 4095) & (1u << (slot*2))) continue;
          instruction = f->config->words+(address+slot)*3;
          if ((instruction[0] & 32768) && !f->config->prepare_export(f->config->context, instruction[0] & 63)) return 0;
        }
      }
    } else if (c.opcode == 7 || c.opcode == 8 || c.opcode == 9 || c.opcode == 11) {
      if ((c.low & 8191) > f->config->control_flow_count) return 0;
      address = c.low & 8191;
      if (c.opcode == 7 || c.opcode == 8) f->features |= FLOW_DISPATCH | FLOW_LOOPS;
      if (c.opcode == 9) f->features |= FLOW_DISPATCH | FLOW_CALLS;
      if (c.opcode == 11 && address <= i) f->features |= FLOW_DISPATCH;
      if (address < f->config->control_flow_count && (c.opcode != 11 || address <= i))
        f->labels[address >> 5] |= 1u << (address & 31);
      if (c.opcode == 9 && i + 1 < f->config->control_flow_count)
        f->labels[(i + 1) >> 5] |= 1u << ((i + 1) & 31);
      if ((c.opcode == 7 || c.opcode == 8) && !f->config->loop_constant) return 0;
      if ((c.opcode == 9 || c.opcode == 11) && !(c.low & 24576) &&
          !f->config->boolean_constant) return 0;
    } else if (c.opcode == 12 && ((c.high >> 9) & 3) == 3 && !f->config->allocation) {
      return 0;
    }
  }
  return 1;
}

static int storage(Flow *f) {
  XeDxilAlu *s = f->config->alu;
  XeDxilValue values[5];
  XeDxilValue one = integer(f, 1);
  XeDxilValue zero = integer(f, 0);
  const struct dxil_type *type;
  unsigned i;
  if (!one || !zero) return 0;
  values[0] = s->previous_scalar; values[1] = s->predicate; values[2] = s->address;
  values[3] = s->killed; values[4] = s->operation_kill;
  for (i = 0; i < 5; ++i) {
    if (!values[i]) return 0;
    f->state[i] = dxil_emit_alloca(f->module, dxil_value_get_type(values[i]), one, 4);
    if (!f->state[i]) return 0;
  }
  type = dxil_module_get_int_type(f->module, 32);
  for (i = 0; i < 4; ++i) {
    if (f->features & FLOW_LOOPS) {
      f->loop_count[i] = dxil_emit_alloca(f->module, type, one, 4);
      f->loop_address[i] = dxil_emit_alloca(f->module, type, one, 4);
      if (!store(f, f->loop_count[i], zero) || !store(f, f->loop_address[i], zero)) return 0;
    }
    if (f->features & FLOW_CALLS) {
      f->return_address[i] = dxil_emit_alloca(f->module, type, one, 4);
      if (!store(f, f->return_address[i], zero)) return 0;
    }
  }
  if (f->features & FLOW_CALLS) {
    f->call_depth = dxil_emit_alloca(f->module, type, one, 4);
    if (!store(f, f->call_depth, zero)) return 0;
  }
  if (f->features & FLOW_DISPATCH) {
    f->program_counter = dxil_emit_alloca(f->module, type, one, 4);
    if (!store(f, f->program_counter, zero)) return 0;
  }
  return save_state(f);
}

int xe_dxil_emit_guest_flow(const XeDxilGuestFlow *config) {
  Flow flow;
  Control c;
  unsigned i;
  unsigned allocation_type;
  unsigned next_test;
  XeDxilValue pc;
  XeDxilValue test;
  if (!config || !config->module || !config->alu || !config->io ||
      !config->io->current_block || !config->words || !config->control_flow_count ||
      config->control_flow_count > 8192 ||
      (config->control_flow_count + 1) / 2 > config->word_count / 3) return 0;
  memset(&flow, 0, sizeof(flow));
  flow.config = config; flow.module = config->module;
  if (config->alu->module != config->module || config->alu->failed ||
      !config->module->cur_emitting_func || config->module->cur_emitting_func->curr_block ||
      !validate(&flow) || !storage(&flow) ||
      !dxil_function_reserve_blocks(flow.module, config->control_flow_count + 1, &flow.base)) goto fail;
  flow.exit = flow.base + config->control_flow_count;
  if (flow.features & FLOW_DISPATCH) {
    if (!new_block(&flow, &flow.dispatch) || !branch(&flow, flow.dispatch) ||
        !begin(&flow, flow.dispatch)) goto fail;
    pc = load(&flow, flow.program_counter);
    if (!pc) goto fail;
    for (i = 0; i < config->control_flow_count; ++i) {
      if (!(flow.labels[i >> 5] & (1u << (i & 31)))) continue;
      test = compare(&flow, DXIL_ICMP_EQ, pc, integer(&flow, i));
      if (!test || !new_block(&flow, &next_test) ||
          !dxil_emit_branch(flow.module, test, target(&flow, i), next_test) || !begin(&flow, next_test)) goto fail;
    }
    if (!branch(&flow, flow.exit)) goto fail;
  } else if (!branch(&flow, target(&flow, 0))) goto fail;
  for (i = 0; i < config->control_flow_count; ++i) {
    if (!begin(&flow, flow.base + i) || !load_state(&flow)) goto fail;
    control(&flow, i, &c);
    if (exec_opcode(c.opcode)) {
      if (!emit_exec(&flow, &c, i)) goto fail;
    } else {
      switch (c.opcode) {
      case 7: if (!emit_loop_start(&flow, &c, i)) goto fail; break;
      case 8: if (!emit_loop_end(&flow, &c, i)) goto fail; break;
      case 9: case 11: if (!emit_call_or_jump(&flow, &c, i)) goto fail; break;
      case 10: if (!emit_return(&flow, i)) goto fail; break;
      case 12:
        allocation_type = (c.high >> 9) & 3;
        if (config->allocation) {
          if (!config->allocation(config->context, allocation_type, c.low & 7, i,
            config->alu, config->io)) goto fail;
        } else if (allocation_type == 3) goto fail;
        if (!save_state(&flow) || !branch(&flow, target(&flow, i + 1))) goto fail;
        break;
      default:
        if (!save_state(&flow) || !branch(&flow, target(&flow, i + 1))) goto fail;
        break;
      }
    }
  }
  if (!begin(&flow, flow.exit) || !load_state(&flow)) goto fail;
  return !config->alu->failed;
fail:
  config->alu->failed = 1;
  return 0;
}
