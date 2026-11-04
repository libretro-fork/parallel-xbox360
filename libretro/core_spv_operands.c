#include "core_spv_operands.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#ifndef XE_SPV_ALLOC
#define XE_SPV_ALLOC malloc
#define XE_SPV_FREE free
#endif
void xe_spv_operands_free(xe_spv_operands *operands) {
   XE_SPV_FREE(operands->heap);
   xe_spv_operands_init(operands);
}
int xe_spv_operands_reserve(xe_spv_operands *operands, size_t capacity) {
   uint32_t *words;
   uint8_t *flags;
   size_t flag_size;
   uint32_t i;
   if (capacity <= operands->capacity) return 1;
   if (capacity > INT_MAX) return 0;
   flag_size = (capacity + 7) / 8;
   if (capacity > (SIZE_MAX - flag_size) / sizeof(*words)) return 0;
   words = (uint32_t *)XE_SPV_ALLOC(capacity * sizeof(*words) + flag_size);
   if (!words) return 0;
   flags = (uint8_t *)(words + capacity);
   if (operands->count) {
      memcpy(words, xe_spv_operands_const_words(operands), operands->count * sizeof(*words));
      if (operands->heap) {
         memcpy(flags, (const uint8_t *)(operands->heap + operands->capacity),
               (operands->count + 7) / 8);
      } else {
         flags[0] = 0;
         for (i = 0; i < operands->count; ++i)
            if (operands->local_flags[i]) flags[0] |= (uint8_t)(1u << i);
      }
   }
   XE_SPV_FREE(operands->heap);
   operands->heap = words;
   operands->capacity = (uint32_t)capacity;
   return 1;
}
int xe_spv_operands_grow(xe_spv_operands *operands) {
   uint32_t capacity;
   if (operands->capacity >= INT_MAX) return 0;
   capacity = operands->capacity > INT_MAX / 2 ? INT_MAX : operands->capacity * 2;
   return xe_spv_operands_reserve(operands, capacity);
}
