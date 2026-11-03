/* Direct DXIL resource operations. MSVC C89; BSD license, see LICENSE. */
#include "guest_resources.h"
#include "shader_module.h"
#include <dxil_module.h>
#include <dxil_function.h>

static XeDxilValue failed(XeDxilAlu *s) {
  if (s) s->failed = 1;
  return NULL;
}
static XeDxilValue integer(XeDxilAlu *s, uint32_t n) {
  return dxil_module_get_int32_const(s->module, (int32_t)n);
}
static int integer_type(XeDxilAlu *s, XeDxilValue value, unsigned bits) {
  return value && dxil_value_get_type(value) == dxil_module_get_int_type(s->module, bits);
}
static int active(XeDxilAlu *s) {
  return s && s->module && !s->failed && s->module->cur_emitting_func;
}

XeDxilValue xe_dxil_resource_handle(XeDxilAlu *s, unsigned resource_class, unsigned id, unsigned binding) {
  const struct dxil_func *function;
  XeDxilValue args[5], value;
  if (!active(s) || resource_class > XE_DXIL_RESOURCE_SAMPLER) return failed(s);
  if (s->module->minor_version >= 6) {
    value = xe_dxil_shader_bound_handle((XeDxilShaderModule *)s->module->native_shader_owner,
      resource_class,id,binding);
    return value ? value : failed(s);
  }
  args[0] = integer(s, 57); args[1] = dxil_module_get_int8_const(s->module, (uint8_t)resource_class);
  args[2] = integer(s, id); args[3] = integer(s, binding); args[4] = dxil_module_get_int1_const(s->module, 0);
  function = dxil_get_function(s->module, "dx.op.createHandle", DXIL_NONE);
  if (!function || !args[0] || !args[1] || !args[2] || !args[3] || !args[4]) return failed(s);
  value = dxil_emit_call(s->module, function, args, 5);
  return value ? value : failed(s);
}

XeDxilValue xe_dxil_raw_buffer_load(XeDxilAlu *s, XeDxilValue handle, XeDxilValue byte_address) {
  const struct dxil_func *function;
  XeDxilValue args[4], value;
  if (!active(s) || !handle || !byte_address || !integer_type(s, byte_address, 32))
    return failed(s);
  args[0] = integer(s, 68); args[1] = handle; args[2] = byte_address;
  args[3] = dxil_module_get_undef(s->module, dxil_module_get_int_type(s->module, 32));
  function = dxil_get_function(s->module, "dx.op.bufferLoad", DXIL_I32);
  if (!function || !args[0] || !args[3]) return failed(s);
  value = dxil_emit_call(s->module, function, args, 4);
  value = value ? dxil_emit_extractval(s->module, value, 0) : NULL;
  return value ? value : failed(s);
}

int xe_dxil_raw_buffer_store(XeDxilAlu *s, XeDxilValue handle, XeDxilValue address, XeDxilValue value) {
  const struct dxil_func *function;
  XeDxilValue args[9];
  if (!active(s) || !handle || !integer_type(s, address, 32) || !integer_type(s, value, 32)) {
    failed(s); return 0;
  }
  args[0] = integer(s, 69); args[1] = handle; args[2] = address;
  args[3] = dxil_module_get_undef(s->module, dxil_module_get_int_type(s->module, 32));
  args[4] = value; args[5] = args[3]; args[6] = args[3]; args[7] = args[3];
  args[8] = dxil_module_get_int8_const(s->module, 1);
  function = dxil_get_function(s->module, "dx.op.bufferStore", DXIL_I32);
  if (!function || !args[0] || !args[3] || !args[8] || !dxil_emit_call_void(s->module, function, args, 9)) {
    failed(s); return 0;
  }
  return 1;
}

XeDxilValue xe_dxil_cbuffer_word(XeDxilAlu *s, XeDxilValue handle, XeDxilValue word_index) {
  const struct dxil_func *function;
  XeDxilValue args[3], vector, component, values[4], value, condition;
  unsigned i;
  if (!active(s) || !handle || !word_index || !integer_type(s, word_index, 32))
    return failed(s);
  args[0] = integer(s, 59); args[1] = handle;
  args[2] = dxil_emit_binop(s->module, DXIL_BINOP_LSHR, word_index, integer(s, 2), 0);
  component = dxil_emit_binop(s->module, DXIL_BINOP_AND, word_index, integer(s, 3), 0);
  function = dxil_get_function(s->module, "dx.op.cbufferLoadLegacy", DXIL_I32);
  if (!function || !args[0] || !args[2] || !component) return failed(s);
  vector = dxil_emit_call(s->module, function, args, 3);
  if (!vector) return failed(s);
  for (i = 0; i < 4; ++i) {
    values[i] = dxil_emit_extractval(s->module, vector, i);
    if (!values[i]) return failed(s);
  }
  value = values[0];
  for (i = 1; i < 4; ++i) {
    condition = dxil_emit_cmp(s->module, DXIL_ICMP_EQ, component, integer(s, i));
    value = condition ? dxil_emit_select(s->module, condition, values[i], value) : NULL;
    if (!value) return failed(s);
  }
  return value;
}

static int begin(XeDxilAlu *s, const XeDxilAluIo *io, unsigned block) {
  struct dxil_func_def *function = s->module->cur_emitting_func;
  if (block >= function->num_basic_block_ids || function->basic_block_ids[block] != -1) return 0;
  function->basic_block_ids[block] = (int)function->curr_block;
  *io->current_block = block;
  return 1;
}

XeDxilValue xe_dxil_guarded_raw_load(XeDxilAlu *s, const XeDxilAluIo *io,
                                     XeDxilValue handle, XeDxilValue byte_address,
                                     XeDxilValue condition) {
  unsigned blocks, incoming_blocks[2];
  XeDxilValue values[2], value;
  struct dxil_instr *phi;
  if (!active(s) || !io || !io->current_block || !handle || !byte_address || !condition ||
      !integer_type(s, condition, 1) ||
      !integer_type(s, byte_address, 32)) return failed(s);
  incoming_blocks[0] = *io->current_block;
  values[0] = integer(s, 0);
  if (!values[0] || !dxil_function_reserve_blocks(s->module, 2, &blocks) ||
      !dxil_emit_branch(s->module, condition, blocks, blocks + 1) || !begin(s, io, blocks)) return failed(s);
  incoming_blocks[1] = blocks;
  values[1] = xe_dxil_raw_buffer_load(s, handle, byte_address);
  if (!values[1] || !dxil_emit_branch(s->module, NULL, blocks + 1, 0) || !begin(s, io, blocks + 1))
    return failed(s);
  phi = dxil_emit_phi(s->module, dxil_module_get_int_type(s->module, 32));
  if (!phi || !dxil_phi_add_incoming(phi, values, incoming_blocks, 2)) return failed(s);
  value = dxil_instr_get_return_value(phi);
  return value ? value : failed(s);
}

XeDxilValue xe_dxil_vertex_resource_constant(void *context, unsigned index, unsigned word) {
  XeDxilVertexResources *resources = (XeDxilVertexResources *)context;
  if (!resources) return NULL;
  if (!active(resources->alu) || index >= 96 || word > 1) return failed(resources->alu);
  return xe_dxil_cbuffer_word(resources->alu, resources->fetch_constants,
    integer(resources->alu, index * 2 + word));
}

XeDxilValue xe_dxil_vertex_resource_load(void *context, XeDxilValue address, XeDxilValue in_bounds) {
  XeDxilVertexResources *resources = (XeDxilVertexResources *)context;
  XeDxilAlu *s;
  XeDxilValue physical, condition, bytes;
  if (!resources) return NULL;
  s = resources->alu;
  if (!active(s) || !address || !in_bounds || !integer_type(s, address, 32) ||
      !integer_type(s, in_bounds, 1)) return failed(s);
  physical = dxil_emit_cmp(s->module, DXIL_ICMP_ULT, address, integer(s, 0x08000000));
  condition = physical ? dxil_emit_binop(s->module, DXIL_BINOP_AND, physical, in_bounds, 0) : NULL;
  bytes = dxil_emit_binop(s->module, DXIL_BINOP_SHL, address, integer(s, 2), 0);
  if (!condition || !bytes) return failed(s);
  return xe_dxil_guarded_raw_load(s, resources->io, resources->shared_memory, bytes, condition);
}
