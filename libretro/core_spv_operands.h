/* Owned SPIR-V operand words/flags; no shallow copies. Views expire on growth/free. */
#ifndef XE_CORE_SPV_OPERANDS_H
#define XE_CORE_SPV_OPERANDS_H
#include <stdint.h>
#include <stddef.h>
#include <retro_inline.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xe_spv_operands {
   uint32_t *heap;
   uint32_t count, capacity;
   uint32_t local_words[4];
   uint8_t local_flags[4];
} xe_spv_operands;
static INLINE void xe_spv_operands_init(xe_spv_operands *operands) {
   operands->heap = NULL;
   operands->count = 0;
   operands->capacity = 4;
}
void xe_spv_operands_free(xe_spv_operands *operands);
int xe_spv_operands_reserve(xe_spv_operands *operands, size_t capacity);
int xe_spv_operands_grow(xe_spv_operands *operands);
static INLINE uint32_t *xe_spv_operands_words(xe_spv_operands *operands) {
   return operands->heap ? operands->heap : operands->local_words;
}
static INLINE const uint32_t *xe_spv_operands_const_words(const xe_spv_operands *operands) {
   return operands->heap ? operands->heap : operands->local_words;
}
static INLINE int xe_spv_operands_is_id(const xe_spv_operands *operands, uint32_t index) {
   const uint8_t *flags;
   if (!operands->heap) return operands->local_flags[index] != 0;
   flags = (const uint8_t *)(operands->heap + operands->capacity);
   return (flags[index >> 3] >> (index & 7)) & 1;
}
static INLINE int xe_spv_operands_append(xe_spv_operands *operands, uint32_t word, int is_id) {
   uint8_t *flags;
   if (operands->count == operands->capacity && !xe_spv_operands_grow(operands)) return 0;
   xe_spv_operands_words(operands)[operands->count] = word;
   if (operands->heap) {
      flags = (uint8_t *)(operands->heap + operands->capacity);
      if (!(operands->count & 7)) flags[operands->count >> 3] = 0;
      if (is_id) flags[operands->count >> 3] |= (uint8_t)(1u << (operands->count & 7));
   } else operands->local_flags[operands->count] = (uint8_t)(is_id != 0);
   ++operands->count;
   return 1;
}
#ifdef __cplusplus
}
#endif
#endif
