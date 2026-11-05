/* Single-owner shader arguments. Views expire on mutation or close. Zero-init
 * before first use. Growth retains capacity; ordinary short lists stay inline. */
#ifndef XE_CORE_SPV_SCRATCH_H
#define XE_CORE_SPV_SCRATCH_H
#include "core_spv_words.h"
#include <stdlib.h>
#include <string.h>
#ifndef XE_SPV_SCRATCH_ALLOC
#define XE_SPV_SCRATCH_ALLOC malloc
#define XE_SPV_SCRATCH_FREE free
#endif
enum { XE_SPV_SCRATCH_INLINE_WORDS = 64 };
typedef struct xe_spv_scratch {
   uint32_t *heap;
   size_t count, capacity;
   uint32_t local[XE_SPV_SCRATCH_INLINE_WORDS];
} xe_spv_scratch;
static INLINE uint32_t *xe_spv_scratch_data(xe_spv_scratch *scratch)
{
   return scratch->heap ? scratch->heap : scratch->local;
}
static INLINE xe_spv_word_view xe_spv_scratch_view(const xe_spv_scratch *scratch)
{
   return xe_spv_words_view(scratch->heap ? scratch->heap : scratch->local,
         scratch->count);
}
static INLINE size_t xe_spv_scratch_capacity(const xe_spv_scratch *scratch)
{
   return scratch->heap ? scratch->capacity : XE_SPV_SCRATCH_INLINE_WORDS;
}
static INLINE void xe_spv_scratch_close(xe_spv_scratch *scratch)
{
   XE_SPV_SCRATCH_FREE(scratch->heap);
   scratch->heap = NULL;
   scratch->count = scratch->capacity = 0;
}
static INLINE int xe_spv_scratch_reserve(xe_spv_scratch *scratch, size_t count)
{
   uint32_t *words;
   size_t capacity = xe_spv_scratch_capacity(scratch);
   if (count <= capacity) return 1;
   if (count > (size_t)-1 / sizeof(*words)) return 0;
   if (capacity <= (size_t)-1 / sizeof(*words) / 2 && count < capacity * 2)
      count = capacity * 2;
   words = (uint32_t *)XE_SPV_SCRATCH_ALLOC(count * sizeof(*words));
   if (!words) return 0;
   if (scratch->count)
      memcpy(words, xe_spv_scratch_data(scratch), scratch->count * sizeof(*words));
   XE_SPV_SCRATCH_FREE(scratch->heap);
   scratch->heap = words;
   scratch->capacity = count;
   return 1;
}
static INLINE int xe_spv_scratch_push(xe_spv_scratch *scratch, uint32_t word)
{
   if (scratch->count == (size_t)-1 ||
       !xe_spv_scratch_reserve(scratch, scratch->count + 1)) return 0;
   xe_spv_scratch_data(scratch)[scratch->count++] = word;
   return 1;
}
static INLINE int xe_spv_scratch_resize(xe_spv_scratch *scratch, size_t count,
      uint32_t fill)
{
   size_t i;
   uint32_t *words;
   if (!xe_spv_scratch_reserve(scratch, count)) return 0;
   words = xe_spv_scratch_data(scratch);
   for (i = scratch->count; i < count; ++i) words[i] = fill;
   scratch->count = count;
   return 1;
}
static INLINE int xe_spv_scratch_fill(xe_spv_scratch *scratch, size_t count,
      uint32_t fill)
{
   if (count > (size_t)-1 - scratch->count) return 0;
   return xe_spv_scratch_resize(scratch, scratch->count + count, fill);
}
#endif
