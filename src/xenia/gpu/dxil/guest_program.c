/* Direct guest program assembly. MSVC C89; BSD license.
 * Register addressing and constant packing follow Xenia's guest translator. */
#include "guest_program.h"
#include <dxil_module.h>
#include <stdlib.h>
#include <string.h>
struct XeDxilGuestProgram {
  XeDxilGuestProgramOptions options;
  XeDxilAlu alu;
  XeDxilAluIo io;
  XeDxilVertexResources resources;
  XeDxilVertexFetch vertex;
  XeDxilTextureFetch texture;
  XeDxilValue registers, exports;
  uint64_t initialized_exports;
  unsigned current_block, serialized, emitted;
};
static XeDxilValue ic(XeDxilGuestProgram *p, unsigned v) { return dxil_module_get_int32_const(p->alu.module, (int32_t)v); }
static XeDxilValue bin(XeDxilGuestProgram *p, enum dxil_bin_opcode op, XeDxilValue a, XeDxilValue b) {
  XeDxilValue value = a && b ? dxil_emit_binop(p->alu.module, op, a, b, 0) : NULL;
  if (!value) p->alu.failed = 1;
  return value;
}
static XeDxilValue clamp_index(XeDxilGuestProgram *p, unsigned index, XeDxilValue offset, unsigned count) {
  XeDxilValue value = ic(p, index), condition;
  if (!count) return NULL;
  if (!offset) return index < count ? value : NULL;
  value = bin(p, DXIL_BINOP_ADD, value, offset);
  condition = value ? dxil_emit_cmp(p->alu.module, DXIL_ICMP_SLT, value, ic(p, 0)) : NULL;
  value = condition ? dxil_emit_select(p->alu.module, condition, ic(p, 0), value) : NULL;
  condition = value ? dxil_emit_cmp(p->alu.module, DXIL_ICMP_UGT, value, ic(p, count-1)) : NULL;
  return condition ? dxil_emit_select(p->alu.module, condition, ic(p, count-1), value) : NULL;
}
static XeDxilValue pointer(XeDxilGuestProgram *p, XeDxilValue base, unsigned index,
    unsigned component, XeDxilValue offset, unsigned count) {
  XeDxilValue args[3], addressed = clamp_index(p, index, offset, count);
  if (!base || !addressed || component >= 4) return NULL;
  args[0] = base; args[1] = ic(p, 0);
  args[2] = bin(p, DXIL_BINOP_ADD, bin(p, DXIL_BINOP_MUL, addressed, ic(p, 4)), ic(p, component));
  return args[2] ? dxil_emit_gep_inbounds(p->alu.module, args, 3) : NULL;
}
static unsigned bit_count(uint64_t v) {
  unsigned count = 0;
  while (v) { v &= v-1; ++count; }
  return count;
}
static int initialize_export(XeDxilGuestProgram *p, unsigned index) {
  XeDxilValue destination, zero;
  unsigned component;
  if (index >= 64) return 0;
  if (p->initialized_exports & ((uint64_t)1 << index)) return 1;
  if (p->alu.module->cur_emitting_func->curr_block) return 0;
  zero = dxil_module_get_float_const(p->alu.module, 0.0f);
  for (component = 0; component < 4; ++component) {
    destination = pointer(p, p->exports, index, component, NULL, 64);
    if (!destination || !zero || !dxil_emit_store(p->alu.module, zero, destination, 4, 0)) return 0;
  }
  p->initialized_exports |= (uint64_t)1 << index;
  return 1;
}
static int prepare_export(void *context, unsigned index) {
  return initialize_export((XeDxilGuestProgram *)context, index);
}
static XeDxilValue read_value(void *context, unsigned constant, unsigned index, unsigned component, XeDxilValue offset) {
  XeDxilGuestProgram *p = (XeDxilGuestProgram *)context;
  XeDxilValue value;
  unsigned block, bit, packed, i;
  if (component >= 4) goto fail;
  if (!constant) {
    value = pointer(p, p->registers, index, component, offset, p->options.register_count);
    value = value ? dxil_emit_load(p->alu.module, value, 4, 0) : NULL;
  } else {
    if (!p->options.float_constants || (offset && !p->options.float_dynamic_addressing)) goto fail;
    packed = index;
    if (!offset && p->options.float_bitmap && !p->options.float_dynamic_addressing) {
      if (index >= 256) goto fail;
      block = index/64; bit = index%64;
      if (!(p->options.float_bitmap[block] & ((uint64_t)1 << bit))) goto fail;
      packed = 0;
      for (i = 0; i < block; ++i) packed += bit_count(p->options.float_bitmap[i]);
      packed += bit_count(p->options.float_bitmap[block] & (((uint64_t)1 << bit)-1));
    }
    value = clamp_index(p, packed, offset, p->options.float_constant_count);
    if (!value) goto fail;
    value = bin(p, DXIL_BINOP_ADD, bin(p, DXIL_BINOP_MUL, value, ic(p, 4)), ic(p, component));
    value = xe_dxil_cbuffer_word(&p->alu, p->options.float_constants, value);
    value = value ? dxil_emit_cast(p->alu.module, DXIL_CAST_BITCAST,
      dxil_module_get_float_type(p->alu.module, 32), value) : NULL;
  }
  if (value) return value;
fail:
  p->alu.failed = 1; return NULL;
}
static int write_value(void *context, unsigned exporting, unsigned index,
    unsigned mask, XeDxilValue values[4], XeDxilValue offset) {
  XeDxilGuestProgram *p = (XeDxilGuestProgram *)context;
  XeDxilValue destination;
  unsigned i;
  if (mask > 15 || !values || (exporting && offset)) goto fail;
  if (exporting && mask && !initialize_export(p, index)) goto fail;
  for (i = 0; i < 4; ++i) if (mask & (1u<<i)) {
    destination = pointer(p, exporting ? p->exports : p->registers, index, i,
      offset, exporting ? 64 : p->options.register_count);
    if (!destination || !values[i] || !dxil_emit_store(p->alu.module, values[i], destination, 4, 0)) goto fail;
  }
  if (exporting && mask && p->options.export_written &&
      !p->options.export_written(p->options.context, p, index, mask)) goto fail;
  return 1;
fail:
  p->alu.failed = 1; return 0;
}
static int kill_value(void *context, XeDxilValue condition) {
  XeDxilGuestProgram *p = (XeDxilGuestProgram *)context;
  return p->options.kill && p->options.kill(p->options.context, &p->alu, &p->io, condition);
}
static XeDxilValue boolean_constant(void *context, unsigned index) {
  XeDxilGuestProgram *p = (XeDxilGuestProgram *)context;
  XeDxilValue value;
  if (!p->options.bool_loop_constants || index >= 256) return NULL;
  value = xe_dxil_cbuffer_word(&p->alu, p->options.bool_loop_constants, ic(p, index/32));
  value = bin(p, DXIL_BINOP_AND, value, ic(p, 1u << (index%32)));
  return value ? dxil_emit_cmp(p->alu.module, DXIL_ICMP_NE, value, ic(p, 0)) : NULL;
}
static XeDxilValue loop_constant(void *context, unsigned index) {
  XeDxilGuestProgram *p = (XeDxilGuestProgram *)context;
  return p->options.bool_loop_constants && index < 32 ?
    xe_dxil_cbuffer_word(&p->alu, p->options.bool_loop_constants, ic(p, 8+index)) : NULL;
}
static int texture_fetch(void *context, const XeDxilTextureInstruction *instruction,
    const XeDxilTextureOperands *operands, XeDxilValue values[4]) {
  XeDxilGuestProgram *p = (XeDxilGuestProgram *)context;
  return p->options.texture && p->options.texture(p->options.context, &p->alu, &p->io,
    instruction, operands, p->serialized, values);
}
static int fetch(void *context, const uint32_t *words, unsigned serialized, XeDxilAlu *alu, XeDxilAluIo *io) {
  XeDxilGuestProgram *p = (XeDxilGuestProgram *)context;
  XeDxilVertexInstruction vertex;
  XeDxilTextureInstruction texture;
  p->serialized = serialized;
  if (!(words[0] & 31)) return p->options.fetch_constants && p->options.shared_memory &&
    xe_dxil_decode_vertex(words, 3, &vertex) && xe_dxil_emit_vertex_fetch(alu, &vertex, io, &p->vertex);
  return xe_dxil_decode_texture(words, 3, &texture) && xe_dxil_emit_texture_fetch(alu, &texture, io, &p->texture);
}
static int allocation(void *context, unsigned type, unsigned size, unsigned cf_index, XeDxilAlu *alu, XeDxilAluIo *io) {
  XeDxilGuestProgram *p = (XeDxilGuestProgram *)context;
  (void)alu; (void)io;
  return p->options.allocation ? p->options.allocation(p->options.context, p, type, size, cf_index) : type != 3;
}
XeDxilGuestProgram *xe_dxil_guest_program_create(struct dxil_module *module, const XeDxilGuestProgramOptions *options) {
  XeDxilGuestProgram *p;
  const struct dxil_type *type;
  XeDxilValue zero[4];
  unsigned i;
  if (!module || !module->cur_emitting_func || module->cur_emitting_func->curr_block ||
      !options || options->register_count > 256 || options->float_constant_count > 256 ||
      (options->float_dynamic_addressing && options->float_constant_count != 256)) return NULL;
  p = (XeDxilGuestProgram *)calloc(1, sizeof(*p)); if (!p) return NULL;
  p->options = *options; xe_dxil_alu_init(&p->alu, module);
  p->io.context = p; p->io.read = read_value; p->io.write = write_value; p->io.kill = kill_value;
  p->io.current_block = &p->current_block; p->io.loop_address = ic(p, 0);
  if (options->register_count) {
    type = dxil_module_get_array_type(module, dxil_module_get_float_type(module, 32), options->register_count*4);
    p->registers = type ? dxil_emit_alloca(module, type, ic(p, 1), 4) : NULL;
    if (!p->registers) goto fail;
  }
  type = dxil_module_get_array_type(module, dxil_module_get_float_type(module, 32), 64*4);
  p->exports = type ? dxil_emit_alloca(module, type, ic(p, 1), 4) : NULL;
  if (!p->exports) goto fail;
  for (i = 0; i < 4; ++i) zero[i] = dxil_module_get_float_const(module, 0.0f);
  for (i = 0; i < options->register_count; ++i) if (!write_value(p, 0, i, 15, zero, NULL)) goto fail;
  p->resources.alu = &p->alu; p->resources.io = &p->io;
  p->resources.fetch_constants = options->fetch_constants; p->resources.shared_memory = options->shared_memory;
  p->vertex.context = &p->resources; p->vertex.constant = xe_dxil_vertex_resource_constant; p->vertex.load = xe_dxil_vertex_resource_load;
  p->texture.context = p; p->texture.fetch = texture_fetch;
  if (!xe_dxil_vertex_fetch_init(&p->alu, &p->vertex) || !xe_dxil_texture_fetch_init(&p->alu, &p->texture) || p->alu.failed) goto fail;
  return p;
fail:
  free(p); return NULL;
}
void xe_dxil_guest_program_destroy(XeDxilGuestProgram *p) { free(p); }
XeDxilAlu *xe_dxil_guest_program_alu(XeDxilGuestProgram *p) { return p ? &p->alu : NULL; }
XeDxilAluIo *xe_dxil_guest_program_io(XeDxilGuestProgram *p) { return p ? &p->io : NULL; }
int xe_dxil_guest_program_register(XeDxilGuestProgram *p, unsigned index, unsigned mask, XeDxilValue values[4]) {
  return p && write_value(p, 0, index, mask, values, NULL);
}
XeDxilValue xe_dxil_guest_program_export(XeDxilGuestProgram *p, unsigned index, unsigned component) {
  XeDxilValue address;
  if (!p || p->alu.failed) return NULL;
  if (index >= 64 || component >= 4) return NULL;
  if (!(p->initialized_exports & ((uint64_t)1 << index))) return dxil_module_get_float_const(p->alu.module, 0.0f);
  address = pointer(p, p->exports, index, component, NULL, 64);
  return address ? dxil_emit_load(p->alu.module, address, 4, 0) : NULL;
}
int xe_dxil_guest_program_emit(XeDxilGuestProgram *p, const uint32_t *words, size_t word_count, unsigned cf_count) {
  XeDxilGuestFlow flow;
  if (!p || p->alu.failed || p->emitted) return 0;
  p->emitted = 1; memset(&flow, 0, sizeof(flow));
  flow.module = p->alu.module; flow.alu = &p->alu; flow.io = &p->io;
  flow.words = words; flow.word_count = word_count; flow.control_flow_count = cf_count;
  flow.context = p; flow.boolean_constant = boolean_constant; flow.loop_constant = loop_constant;
  flow.fetch = fetch; flow.allocation = allocation;
  flow.prepare_export = prepare_export;
  return xe_dxil_emit_guest_flow(&flow) && !p->alu.failed;
}
