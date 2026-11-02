/* Xbox shader control-flow lowering. MSVC C89; BSD license, see LICENSE. */
#ifndef XE_GPU_DXIL_GUEST_FLOW_H
#define XE_GPU_DXIL_GUEST_FLOW_H
#include "guest_alu_instruction.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct XeDxilGuestFlow {
  struct dxil_module *module;
  XeDxilAlu *alu;
  XeDxilAluIo *io;
  const uint32_t *words;
  size_t word_count;
  unsigned control_flow_count;
  void *context;
  /* Boolean constants return i1; packed loop constants return i32. */
  XeDxilValue (*boolean_constant)(void *context, unsigned index);
  XeDxilValue (*loop_constant)(void *context, unsigned index);
  int (*fetch)(void *context, const uint32_t *words, unsigned serialized,
                XeDxilAlu *alu, XeDxilAluIo *io);
  int (*allocation)(void *context, unsigned type, unsigned size,
                     unsigned control_flow_index, XeDxilAlu *alu, XeDxilAluIo *io);
  /* Optional entry-block storage preparation for referenced export slots.
   * Called during validation, before control-flow blocks are emitted. */
  int (*prepare_export)(void *context, unsigned index);
} XeDxilGuestFlow;

/* Emit into an entry block, after the stage adapter creates storage/bindings.
 * The source is read directly, without copying or serializing another shader
 * format. On success the current block is the joined exit, with ALU/aL state
 * reloaded. The stage adapter then emits exports/epilogue and return.
 * Hooks may reserve blocks and update io->current_block, but must leave a
 * continuation block open. Missing hooks reject instructions that need them. */
int xe_dxil_emit_guest_flow(const XeDxilGuestFlow *flow);

#ifdef __cplusplus
}
#endif
#endif
