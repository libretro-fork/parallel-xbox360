/* Direct Xbox 360 ALU instruction frontend. MSVC C89; BSD, see LICENSE. */
#ifndef XE_GPU_DXIL_GUEST_ALU_INSTRUCTION_H
#define XE_GPU_DXIL_GUEST_ALU_INSTRUCTION_H
#include <stddef.h>
#include "guest_alu.h"

#ifdef __cplusplus
extern "C" {
#endif

enum XeDxilAddressing {
  XE_DXIL_ABSOLUTE = 0, XE_DXIL_LOOP_RELATIVE = 1, XE_DXIL_ADDRESS_RELATIVE = 2
};

typedef struct XeDxilOperand {
  unsigned is_constant;
  unsigned index;
  unsigned addressing;
  unsigned absolute_value;
  unsigned negate;
  unsigned components[4];
} XeDxilOperand;

typedef struct XeDxilAluInstruction {
  unsigned vector_opcode;
  unsigned scalar_opcode;
  unsigned is_export;
  unsigned is_predicated;
  unsigned predicate_condition;
  unsigned vector_destination;
  unsigned scalar_destination;
  unsigned vector_relative;
  unsigned scalar_relative;
  unsigned vector_mask;
  unsigned scalar_mask;
  unsigned zero_mask;
  unsigned one_mask;
  unsigned vector_clamp;
  unsigned scalar_clamp;
  unsigned vector_operand_count;
  unsigned scalar_operand_count;
  XeDxilOperand vector_operands[3];
  XeDxilOperand scalar_operands[2];
} XeDxilAluInstruction;

/* Read/write callbacks emit straight-line code for storage access. Offset is
 * NULL for absolute addressing, otherwise the current aL or a0 SSA value.
 * Exports use raw Xbox export indices; the stage adapter owns their mapping.
 * No destination is written until both co-issued operations are evaluated. */
typedef struct XeDxilAluIo {
  void *context;
  XeDxilValue loop_address;
  XeDxilValue (*read)(void *context, unsigned is_constant, unsigned index,
                      unsigned component, XeDxilValue offset);
  int (*write)(void *context, unsigned is_export, unsigned index,
               unsigned mask, XeDxilValue values[4], XeDxilValue offset);
  /* Required for kill opcodes: flush pending exports and emit discard for
   * condition before destination writes. May add blocks, updating current_block. */
  int (*kill)(void *context, XeDxilValue condition);
  unsigned *current_block;
} XeDxilAluIo;

/* Words are host-endian Xbox ucode dwords, as in Shader::ucode_data(). */
int xe_dxil_decode_alu(const uint32_t *words, size_t word_count,
                       XeDxilAluInstruction *instruction);
/* Predicated instructions require two unused block IDs reserved by the flow
 * frontend. Memory writes occur only in the body; ALU state merges via phi. */
int xe_dxil_emit_alu_instruction(XeDxilAlu *alu,
                                 const XeDxilAluInstruction *instruction,
                                 const XeDxilAluIo *io,
                                 unsigned body_block, unsigned merge_block);

#ifdef __cplusplus
}
#endif
#endif
