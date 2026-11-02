/* Direct guest program assembly. MSVC C89; BSD license. */
#ifndef XE_GPU_DXIL_GUEST_PROGRAM_H
#define XE_GPU_DXIL_GUEST_PROGRAM_H
#include "guest_flow.h"
#include "guest_resources.h"
#include "guest_texture.h"
typedef struct XeDxilGuestProgram XeDxilGuestProgram;
typedef struct XeDxilGuestProgramOptions {
  unsigned register_count, float_constant_count, float_dynamic_addressing;
  /* Optional borrowed four-word bitmap for tightly packed static constants.
   * NULL selects dense constants. Dynamic constants must use all 256 entries. */
  const uint64_t *float_bitmap;
  XeDxilValue float_constants, bool_loop_constants, fetch_constants, shared_memory;
  void *context;
  int (*texture)(void *context, XeDxilAlu *alu, XeDxilAluIo *io,
    const XeDxilTextureInstruction *instruction, const XeDxilTextureOperands *operands,
    unsigned serialized, XeDxilValue values[4]);
  int (*allocation)(void *context, XeDxilGuestProgram *program, unsigned type,
    unsigned size, unsigned control_flow_index);
  int (*export_written)(void *context, XeDxilGuestProgram *program, unsigned index, unsigned mask);
  int (*kill)(void *context, XeDxilAlu *alu, XeDxilAluIo *io, XeDxilValue condition);
} XeDxilGuestProgramOptions;
#ifdef __cplusplus
extern "C" {
#endif
/* Create in the shader entry block. Register/export storage starts at zero;
 * the stage adapter writes vertex IDs or interpolators before emitting ucode.
 * Handles and input words are borrowed, with no intermediate shader format.
 * The adapter owns signatures, binding declarations and export epilogues. */
XeDxilGuestProgram *xe_dxil_guest_program_create(struct dxil_module *module,
  const XeDxilGuestProgramOptions *options);
void xe_dxil_guest_program_destroy(XeDxilGuestProgram *program);
XeDxilAlu *xe_dxil_guest_program_alu(XeDxilGuestProgram *program);
XeDxilAluIo *xe_dxil_guest_program_io(XeDxilGuestProgram *program);
int xe_dxil_guest_program_register(XeDxilGuestProgram *program, unsigned index,
  unsigned mask, XeDxilValue values[4]);
/* Stage defaults, before emit, do not report guest export writes. */
int xe_dxil_guest_program_default_export(XeDxilGuestProgram *program, unsigned index,
  unsigned mask, XeDxilValue values[4]);
XeDxilValue xe_dxil_guest_program_export(XeDxilGuestProgram *program, unsigned index, unsigned component);
int xe_dxil_guest_program_emit(XeDxilGuestProgram *program, const uint32_t *words,
  size_t word_count, unsigned control_flow_count);
#ifdef __cplusplus
}
#endif
#endif
