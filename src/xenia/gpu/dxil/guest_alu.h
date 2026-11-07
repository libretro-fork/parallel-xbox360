/* Xenia: Xbox 360 ALU lowering to raw DXIL. BSD license; see LICENSE. */
#ifndef XE_GPU_DXIL_GUEST_ALU_H
#define XE_GPU_DXIL_GUEST_ALU_H

#include <stdint.h>

struct dxil_module;
struct dxil_value;

#ifdef __cplusplus
extern "C" {
#endif

typedef const struct dxil_value *XeDxilValue;

/* Values are SSA scalars. Operand loading, swizzles, predication and export
 * ordering belong to the instruction frontend. A kill is returned as state
 * so pending memory exports can be committed before emitting discard. */
typedef struct XeDxilAlu {
  struct dxil_module *module;
  XeDxilValue previous_scalar;
  XeDxilValue predicate;
  XeDxilValue address;
  XeDxilValue killed;
  XeDxilValue operation_kill;
  int vertex_round_toward_zero;
  int failed;
} XeDxilAlu;

enum XeDxilVectorOpcode {
  XE_DXIL_V_ADD = 0, XE_DXIL_V_MUL = 1, XE_DXIL_V_MAX = 2,
  XE_DXIL_V_MIN = 3, XE_DXIL_V_SEQ = 4, XE_DXIL_V_SGT = 5,
  XE_DXIL_V_SGE = 6, XE_DXIL_V_SNE = 7, XE_DXIL_V_FRC = 8,
  XE_DXIL_V_TRUNC = 9, XE_DXIL_V_FLOOR = 10, XE_DXIL_V_MAD = 11,
  XE_DXIL_V_CNDEQ = 12, XE_DXIL_V_CNDGE = 13, XE_DXIL_V_CNDGT = 14,
  XE_DXIL_V_DP4 = 15, XE_DXIL_V_DP3 = 16, XE_DXIL_V_DP2ADD = 17,
  XE_DXIL_V_CUBE = 18, XE_DXIL_V_MAX4 = 19,
  XE_DXIL_V_SETP_EQ_PUSH = 20, XE_DXIL_V_SETP_NE_PUSH = 21,
  XE_DXIL_V_SETP_GT_PUSH = 22, XE_DXIL_V_SETP_GE_PUSH = 23,
  XE_DXIL_V_KILL_EQ = 24, XE_DXIL_V_KILL_GT = 25,
  XE_DXIL_V_KILL_GE = 26, XE_DXIL_V_KILL_NE = 27,
  XE_DXIL_V_DST = 28, XE_DXIL_V_MAXA = 29
};

enum XeDxilScalarOpcode {
  XE_DXIL_S_ADD = 0, XE_DXIL_S_ADD_PREV = 1, XE_DXIL_S_MUL = 2,
  XE_DXIL_S_MUL_PREV = 3, XE_DXIL_S_MUL_PREV2 = 4,
  XE_DXIL_S_MAX = 5, XE_DXIL_S_MIN = 6, XE_DXIL_S_SEQ = 7,
  XE_DXIL_S_SGT = 8, XE_DXIL_S_SGE = 9, XE_DXIL_S_SNE = 10,
  XE_DXIL_S_FRC = 11, XE_DXIL_S_TRUNC = 12, XE_DXIL_S_FLOOR = 13,
  XE_DXIL_S_EXP = 14, XE_DXIL_S_LOGC = 15, XE_DXIL_S_LOG = 16,
  XE_DXIL_S_RCPC = 17, XE_DXIL_S_RCPF = 18, XE_DXIL_S_RCP = 19,
  XE_DXIL_S_RSQC = 20, XE_DXIL_S_RSQF = 21, XE_DXIL_S_RSQ = 22,
  XE_DXIL_S_MAXA = 23, XE_DXIL_S_MAXAF = 24,
  XE_DXIL_S_SUB = 25, XE_DXIL_S_SUB_PREV = 26,
  XE_DXIL_S_SETP_EQ = 27, XE_DXIL_S_SETP_NE = 28,
  XE_DXIL_S_SETP_GT = 29, XE_DXIL_S_SETP_GE = 30,
  XE_DXIL_S_SETP_INV = 31, XE_DXIL_S_SETP_POP = 32,
  XE_DXIL_S_SETP_CLR = 33, XE_DXIL_S_SETP_RSTR = 34,
  XE_DXIL_S_KILL_EQ = 35, XE_DXIL_S_KILL_GT = 36,
  XE_DXIL_S_KILL_GE = 37, XE_DXIL_S_KILL_NE = 38,
  XE_DXIL_S_KILL_ONE = 39, XE_DXIL_S_SQRT = 40,
  XE_DXIL_S_MULSC0 = 42, XE_DXIL_S_MULSC1 = 43,
  XE_DXIL_S_ADDSC0 = 44, XE_DXIL_S_ADDSC1 = 45,
  XE_DXIL_S_SUBSC0 = 46, XE_DXIL_S_SUBSC1 = 47,
  XE_DXIL_S_SIN = 48, XE_DXIL_S_COS = 49, XE_DXIL_S_RETAIN_PREV = 50
};

/* Numeric opcodes match the Xbox instruction encoding (ucode.h).
 * identical_components describes operand identity before optimization, not
 * equality at runtime. Cube consumes the ISA-swizzled first operand .zzxy. */
void xe_dxil_alu_init(XeDxilAlu *alu, struct dxil_module *module);
int xe_dxil_alu_vector(XeDxilAlu *alu, unsigned opcode,
                       XeDxilValue operands[3][4], unsigned identical_components,
                       XeDxilValue result[4]);
XeDxilValue xe_dxil_alu_scalar(XeDxilAlu *alu, unsigned opcode,
                              XeDxilValue a, XeDxilValue b,
                              int identical_components);
XeDxilValue xe_dxil_alu_float_bits(XeDxilAlu *alu, uint32_t bits);
XeDxilValue xe_dxil_alu_modify(XeDxilAlu *alu, XeDxilValue value,
                              int absolute_value, int negate_value);
XeDxilValue xe_dxil_alu_saturate(XeDxilAlu *alu, XeDxilValue value);

#ifdef __cplusplus
}
#endif
#endif
