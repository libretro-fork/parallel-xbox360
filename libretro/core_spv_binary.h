/* Reusable raw SPIR-V output. Single owner; borrowed words expire on growth,
 * reset or close. Operand spans must not alias this output allocation. */
#ifndef XE_CORE_SPV_BINARY_H
#define XE_CORE_SPV_BINARY_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xe_spv_binary {
   uint32_t *words;
   size_t count, capacity;
   int failed;
} xe_spv_binary;
int xe_spv_binary_reserve(xe_spv_binary *out, size_t words);
int xe_spv_binary_reserve_exact(xe_spv_binary *out, size_t words);
/* Moves ownership, closes the previous destination, and empties the source. */
void xe_spv_binary_take(xe_spv_binary *out, xe_spv_binary *source);
void xe_spv_binary_reset(xe_spv_binary *out);
void xe_spv_binary_close(xe_spv_binary *out);
void xe_spv_binary_word(xe_spv_binary *out, uint32_t word);
void xe_spv_binary_append(xe_spv_binary *out, const uint32_t *words, size_t count);
void xe_spv_binary_string(xe_spv_binary *out, uint32_t opcode, const char *text);
/* Prefix and text are borrowed for this call and cannot alias output storage.
 * Counted text need not have a terminator; one is encoded in the final words. */
void xe_spv_binary_string_span(xe_spv_binary *out, uint32_t opcode,
      const uint32_t *prefix, size_t prefix_count, const char *text, size_t bytes);
void xe_spv_binary_instruction(xe_spv_binary *out, uint32_t opcode,
      uint32_t type, uint32_t result, const uint32_t *operands, size_t count);
#ifdef __cplusplus
}
#endif
#endif
