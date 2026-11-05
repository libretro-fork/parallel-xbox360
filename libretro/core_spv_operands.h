/* Owned SPIR-V operand words/flags; no shallow copies. Views expire on growth/free. */
#ifndef XE_CORE_SPV_OPERANDS_H
#define XE_CORE_SPV_OPERANDS_H
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#include <assert.h>
#include <retro_inline.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xe_spv_operands {
   uint32_t *heap;
   uint32_t count, capacity;
   uint32_t local_words[4];
   /* With heap storage, local_flags[0] is its pooled-allocation marker instead.
    * Inline flags have already been copied into the heap's packed bitmap. */
   uint8_t local_flags[4];
} xe_spv_operands;
static INLINE void xe_spv_operands_init(xe_spv_operands *operands) {
   operands->heap = NULL;
   operands->count = 0;
   operands->capacity = 4;
}
void xe_spv_operands_free(xe_spv_operands *operands);
int xe_spv_operands_reserve(xe_spv_operands *operands, size_t capacity);
/* Callback borrows a workspace; old is supplied only for pooled storage. It
 * may extend old in place but must never free it. Returned spans are 8-byte
 * aligned and padded to 8 bytes. Their owner outlives all operand payloads. */
typedef void *(*xe_spv_operand_realloc_fn)(void *, void *, size_t, size_t);
int xe_spv_operands_reserve_with(xe_spv_operands *operands, size_t capacity,
      xe_spv_operand_realloc_fn allocate, void *context);
static INLINE size_t xe_spv_operands_growth_capacity(const xe_spv_operands *operands) {
   if (operands->capacity >= INT_MAX) return 0;
   return operands->capacity > INT_MAX / 2 ? INT_MAX : operands->capacity * 2;
}
static INLINE size_t xe_spv_operands_string_capacity(const xe_spv_operands *operands, size_t bytes) {
   size_t added = bytes / 4 + 1;
   if (operands->count > INT_MAX || added > (size_t)INT_MAX - operands->count) return 0;
   return operands->count + added;
}
int xe_spv_operands_grow(xe_spv_operands *operands);
/* Counted text need not be NUL terminated; it cannot alias operand storage.
 * Reserve once and encode in place. Failure preserves the original operands. */
int xe_spv_operands_string(xe_spv_operands *operands, const char *text, size_t bytes);
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
static INLINE void xe_spv_operands_append_reserved(xe_spv_operands *operands, uint32_t word, int is_id) {
   uint8_t *flags;
   assert(operands->count < operands->capacity);
   xe_spv_operands_words(operands)[operands->count] = word;
   if (operands->heap) {
      flags = (uint8_t *)(operands->heap + operands->capacity);
      if (!(operands->count & 7)) flags[operands->count >> 3] = 0;
      if (is_id) flags[operands->count >> 3] |= (uint8_t)(1u << (operands->count & 7));
   } else operands->local_flags[operands->count] = (uint8_t)(is_id != 0);
   ++operands->count;
}
static INLINE int xe_spv_operands_append(xe_spv_operands *operands, uint32_t word, int is_id) {
   if (operands->count == operands->capacity && !xe_spv_operands_grow(operands)) return 0;
   xe_spv_operands_append_reserved(operands, word, is_id);
   return 1;
}
#ifdef __cplusplus
}
#endif
#endif
