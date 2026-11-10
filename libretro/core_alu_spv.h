/* Raw SPIR-V arithmetic foundation for the ready interpreter. MSVC C89. */
#ifndef XE_CORE_ALU_SPV_H
#define XE_CORE_ALU_SPV_H
#include "core_spv_binary.h"
#define XE_ALU_PROGRAM_ABI 1
#ifdef __cplusplus
extern "C" {
#endif
/* Validation entry point, not a complete guest-shader fallback.
 * Each input record is 16 words: three packed instruction words, three
 * address-resolved source vectors, one reserved word. Component-relative
 * swizzles, source absolute/negate modifiers are applied on the GPU. Scalar
 * operations, control flow, stores and stage behavior are not covered.
 * Result records are eight words: four vector result bits, supported flag,
 * and three untouched guards. Unsupported vector opcodes leave result words
 * untouched. This first core supports ADD, MUL and MAD, using RTZ+FTZ and
 * SM3 zero multiplication; NaN payload bits are not specified.
 * One module/pipeline executes arbitrary records, without per-record codegen.
 * The caller owns/reuses output storage; no second module allocation. */
int xe_alu_spv_build(xe_spv_binary *out);
/* Prototype packed EXEC_END program kernel (vertex RTZ arithmetic only).
 * Four storage bindings: descriptors+initial temps, borrowed program words,
 * borrowed constants, output records. Descriptor buffer starts with count,
 * then count records of eight words: code offset/word count, initial temp
 * offset/vec4 count, constant offset/vec4 count, constant window base/size-1.
 * Bit31 of code word count selects big-endian program storage; bits0..30
 * hold the word count. Endian correction occurs at GPU loads, with no host
 * normalized-code copy. Constant/temp words are host-order binary32 bits.
 * Each output has position bits[4], status, export count, accumulated mask,
 * untouched guard. One invocation interprets one descriptor, up to six ALUs.
 * Supports vector ADD/MUL/MAD; scalar ADD, ADD_CONST_0, RETAIN_PREV;
 * direct temps/constants, masks and position exports. No fetch, relative
 * addressing, predicates, clamp, other control flow/stages or ROP yet.
 * Status 1 commits a complete position. 2 is malformed bounds, 3 unsupported,
 * 4 incomplete position; failures write only status. Input bindings are
 * immutable and disjoint from output through GPU completion. Payload owners
 * must outlive that completion. Private register snapshots remain necessary.
 * This validation kernel is not wired into rendering and is not a fallback. */
int xe_alu_program_spv_build(xe_spv_binary *out);
/* Experimental control-flow entry, same descriptor ABI/ownership as above.
 * Supports EXEC/EXEC_END, NOP, unconditional JMP/CALL and RET, with four fixed
 * return addresses. A RET with an empty stack falls through. Unsupported CF,
 * including conditional execution and loops, fails with status3. Out-of-bounds
 * instruction/target reads fail with status2. At most4096 CF steps are executed;
 * status5 reports exhausted work without committing output. This limit requires
 * resumable execution before renderer use; no valid-draw fallback is claimed.
 * The original entry remains separately available for matched comparisons. */
int xe_alu_control_spv_build(xe_spv_binary *out);
#ifdef __cplusplus
}
#endif
#endif
