/* Bounded shader-helper scratch and read-only borrowed words. No heap owner.
 * A view expires when its source words are modified or leave scope. */
#ifndef XE_CORE_SPV_WORDS_H
#define XE_CORE_SPV_WORDS_H
#include <stddef.h>
#include <stdint.h>
#include <retro_inline.h>
typedef struct xe_spv_word_view {
   const uint32_t *words;
   size_t count;
} xe_spv_word_view;
enum { XE_SPV_HELPER_WORD_CAPACITY = 16 };
typedef struct xe_spv_word_list {
   uint32_t words[XE_SPV_HELPER_WORD_CAPACITY];
   size_t count;
} xe_spv_word_list;
static INLINE xe_spv_word_view xe_spv_words_view(const uint32_t *words, size_t count)
{
   xe_spv_word_view view;
   view.words = words;
   view.count = count;
   return view;
}
static INLINE xe_spv_word_view xe_spv_word_list_view(const xe_spv_word_list *list)
{
   return xe_spv_words_view(list->words, list->count);
}
static INLINE int xe_spv_word_list_push(xe_spv_word_list *list, uint32_t word)
{
   if (list->count >= XE_SPV_HELPER_WORD_CAPACITY) return 0;
   list->words[list->count++] = word;
   return 1;
}
#endif
