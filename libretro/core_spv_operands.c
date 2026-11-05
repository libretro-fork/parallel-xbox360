#include "core_spv_operands.h"
#include "core_spv_string.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#ifndef XE_SPV_ALLOC
#define XE_SPV_ALLOC malloc
#define XE_SPV_FREE free
#endif
/* Only pooled spans have guaranteed rounded padding beyond their flags. */
#if defined(__SANITIZE_ADDRESS__)
#define XE_SPV_OPERANDS_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define XE_SPV_OPERANDS_ASAN 1
#endif
#endif
#ifdef XE_SPV_OPERANDS_ASAN
#include <sanitizer/asan_interface.h>
#endif
static void release_heap(xe_spv_operands *operands) {
   if (!operands->heap) return;
   if (!operands->local_flags[0]) XE_SPV_FREE(operands->heap);
#ifdef XE_SPV_OPERANDS_ASAN
   else {
      size_t bytes = operands->capacity * sizeof(uint32_t) + (operands->capacity + 7u) / 8u;
      __asan_poison_memory_region(operands->heap, (bytes + 7) & ~(size_t)7);
   }
#endif
}
void xe_spv_operands_free(xe_spv_operands *operands) {
   release_heap(operands);
   xe_spv_operands_init(operands);
}
int xe_spv_operands_reserve_with(xe_spv_operands *operands, size_t capacity,
      xe_spv_operand_realloc_fn resize_storage, void *context) {
   uint32_t *words;
   uint8_t *flags;
   size_t flag_size, bytes, old_bytes = 0;
   uint32_t i;
   if (capacity <= operands->capacity) return 1;
   if (capacity > INT_MAX) return 0;
   flag_size = (capacity + 7) / 8;
   if (capacity > (SIZE_MAX - flag_size) / sizeof(*words)) return 0;
   bytes = capacity * sizeof(*words) + flag_size;
   if (operands->heap && operands->local_flags[0])
      old_bytes = operands->capacity * sizeof(*words) + (operands->capacity + 7u) / 8u;
   words = resize_storage ? (uint32_t *)resize_storage(context, old_bytes ? operands->heap : NULL,
         old_bytes, bytes) : (uint32_t *)XE_SPV_ALLOC(bytes);
   if (!words) return 0;
   flags = (uint8_t *)(words + capacity);
   if (operands->count) {
      if (words != operands->heap)
         memcpy(words, xe_spv_operands_const_words(operands), operands->count * sizeof(*words));
      if (operands->heap) {
         memmove(flags, (const uint8_t *)(operands->heap + operands->capacity),
               (operands->count + 7) / 8);
      } else {
         flags[0] = 0;
         for (i = 0; i < operands->count; ++i)
            if (operands->local_flags[i]) flags[0] |= (uint8_t)(1u << i);
      }
   }
   if (words != operands->heap) release_heap(operands);
   operands->heap = words;
   operands->capacity = (uint32_t)capacity;
   operands->local_flags[0] = (uint8_t)(resize_storage != NULL);
   return 1;
}
int xe_spv_operands_reserve(xe_spv_operands *operands, size_t capacity) {
   return xe_spv_operands_reserve_with(operands, capacity, NULL, NULL);
}
int xe_spv_operands_grow(xe_spv_operands *operands) {
   size_t capacity = xe_spv_operands_growth_capacity(operands);
   if (!capacity) return 0;
   return xe_spv_operands_reserve(operands, capacity);
}
int xe_spv_operands_string(xe_spv_operands *operands, const char *text, size_t bytes)
{
   size_t added = bytes / 4 + 1, first = operands->count, end, byte;
   uint8_t *flags;
   if ((bytes && !text) || first > INT_MAX || added > (size_t)INT_MAX - first ||
       !xe_spv_operands_reserve(operands, first + added)) return 0;
   xe_spv_string_encode(xe_spv_operands_words(operands) + first, text, bytes);
   if (operands->heap) {
      flags = (uint8_t *)(operands->heap + operands->capacity);
      byte = first / 8;
      end = (first + added + 7) / 8;
      if (first & 7) flags[byte] &= (uint8_t)((1u << (first & 7)) - 1);
      else flags[byte] = 0;
      if (end > byte + 1) memset(flags + byte + 1, 0, end - byte - 1);
   } else memset(operands->local_flags + first, 0, added);
   operands->count = (uint32_t)(first + added);
   return 1;
}
