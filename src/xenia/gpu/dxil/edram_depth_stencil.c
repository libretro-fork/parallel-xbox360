/* Native EDRAM depth/stencil operations. MSVC C89; BSD license.
 * Guest rules follow Xenia's render-backend implementation. */
#include "edram_depth_stencil.h"
#include <dxil_module.h>
static XeDxilValue ic(XeDxilAlu *s, uint32_t value) {
  return dxil_module_get_int32_const(s->module, (int32_t)value);
}
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
static XeDxilValue pick(XeDxilAlu *s, XeDxilValue condition, XeDxilValue a, XeDxilValue b) {
  XeDxilValue value = condition && a && b ? dxil_emit_select(s->module, condition, a, b) : NULL;
  if (!value) s->failed = 1;
  return value;
}
static XeDxilValue test(XeDxilAlu *s, XeDxilValue function, XeDxilValue a, XeDxilValue b) {
  XeDxilValue relation = pick(s, cmp(s, DXIL_ICMP_ULT, a, b), ic(s, 1),
    pick(s, cmp(s, DXIL_ICMP_EQ, a, b), ic(s, 2), ic(s, 4)));
  return cmp(s, DXIL_ICMP_NE, bin(s, DXIL_BINOP_AND, function, relation), ic(s, 0));
}
static XeDxilValue stencil_operation(XeDxilAlu *s, XeDxilValue op, XeDxilValue old, XeDxilValue reference) {
  XeDxilValue operations[8], increased, decreased, value;
  unsigned i;
  increased = bin(s, DXIL_BINOP_ADD, old, ic(s, 1));
  decreased = bin(s, DXIL_BINOP_SUB, old, ic(s, 1));
  operations[0] = old; operations[1] = ic(s, 0); operations[2] = reference;
  operations[3] = pick(s, cmp(s, DXIL_ICMP_EQ, old, ic(s, 255)), old, increased);
  operations[4] = pick(s, cmp(s, DXIL_ICMP_EQ, old, ic(s, 0)), old, decreased);
  operations[5] = bin(s, DXIL_BINOP_XOR, old, ic(s, 255));
  operations[6] = bin(s, DXIL_BINOP_AND, increased, ic(s, 255));
  operations[7] = bin(s, DXIL_BINOP_AND, decreased, ic(s, 255));
  value = operations[0];
  for (i = 1; i < 8; ++i) value = pick(s, cmp(s, DXIL_ICMP_EQ, op, ic(s, i)), operations[i], value);
  return value;
}
int xe_dxil_edram_depth_stencil(XeDxilAlu *s, const XeDxilDepthStencilState *state,
    XeDxilValue previous, XeDxilValue depth, XeDxilDepthStencilResult *result) {
  XeDxilValue old_depth, old_stencil, reference, read_mask, write_mask, depth_passed, stencil_passed;
  XeDxilValue stencil_enabled, depth_enabled, write_depth, stencil, operation, zero, one;
  if (!s || !s->module || s->failed || !state || !result || !previous || !depth) return 0;
  zero = ic(s, 0); one = dxil_module_get_int1_const(s->module, 1);
  old_depth = bin(s, DXIL_BINOP_LSHR, previous, ic(s, 8));
  old_stencil = bin(s, DXIL_BINOP_AND, previous, ic(s, 255));
  depth = bin(s, DXIL_BINOP_AND, depth, ic(s, 0xffffff));
  reference = bin(s, DXIL_BINOP_AND, state->reference, ic(s, 255));
  read_mask = bin(s, DXIL_BINOP_AND, state->read_mask, ic(s, 255));
  write_mask = bin(s, DXIL_BINOP_AND, state->write_mask, ic(s, 255));
  stencil_enabled = cmp(s, DXIL_ICMP_NE, state->stencil_enabled, zero);
  depth_enabled = cmp(s, DXIL_ICMP_NE, state->depth_enabled, zero);
  depth_passed = pick(s, depth_enabled, test(s, state->depth_function, depth, old_depth), one);
  stencil_passed = pick(s, stencil_enabled, test(s, state->stencil_function,
    bin(s, DXIL_BINOP_AND, reference, read_mask), bin(s, DXIL_BINOP_AND, old_stencil, read_mask)), one);
  result->passed = bin(s, DXIL_BINOP_AND, stencil_passed, depth_passed);
  result->stencil_failed = bin(s, DXIL_BINOP_XOR, stencil_passed, one);
  result->depth_failed = bin(s, DXIL_BINOP_AND, stencil_passed, bin(s, DXIL_BINOP_XOR, depth_passed, one));
  operation = pick(s, stencil_passed, pick(s, depth_passed, state->depth_pass, state->depth_fail), state->stencil_fail);
  stencil = stencil_operation(s, operation, old_stencil, reference);
  stencil = bin(s, DXIL_BINOP_OR, bin(s, DXIL_BINOP_AND, stencil, write_mask),
    bin(s, DXIL_BINOP_AND, old_stencil, bin(s, DXIL_BINOP_XOR, write_mask, ic(s, 255))));
  stencil = pick(s, stencil_enabled, stencil, old_stencil);
  write_depth = bin(s, DXIL_BINOP_AND, depth_enabled,
    bin(s, DXIL_BINOP_AND, result->passed, cmp(s, DXIL_ICMP_NE, state->depth_write, zero)));
  depth = pick(s, write_depth, depth, old_depth);
  result->packed = bin(s, DXIL_BINOP_OR, bin(s, DXIL_BINOP_SHL, depth, ic(s, 8)), stencil);
  result->changed = cmp(s, DXIL_ICMP_NE, previous, result->packed);
  return !s->failed && result->packed && result->passed && result->stencil_failed && result->depth_failed && result->changed;
}
