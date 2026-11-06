/* Bounded shader-helper scratch and read-only borrowed words. No heap owner.
 * A view expires when its source words are modified or leave scope. */
#ifndef XE_CORE_SPV_WORDS_H
#define XE_CORE_SPV_WORDS_H
#include <stddef.h>
#include <stdint.h>
#include <retro_inline.h>
#include <assert.h>
typedef struct xe_spv_word_view {
   const uint32_t *words;
   size_t count;
} xe_spv_word_view;
/* Logical operand source: borrow a contiguous view or one repeated word.
 * Neither mode owns data; repeated count never denotes a C array extent. */
typedef struct xe_spv_word_source {
   const uint32_t *words;
   size_t count;
   int repeated;
} xe_spv_word_source;
static INLINE xe_spv_word_source xe_spv_source_view(xe_spv_word_view view) {
   xe_spv_word_source source;
   source.words = view.words; source.count = view.count; source.repeated = 0;
   return source;
}
static INLINE xe_spv_word_source xe_spv_source_repeat(const uint32_t *word, size_t count) {
   xe_spv_word_source source;
   source.words = word; source.count = count; source.repeated = 1;
   return source;
}
static INLINE int xe_spv_source_all_equal(xe_spv_word_source source) {
   size_t i;
   if (!source.count) return 0;
   assert(source.words);
   if (source.repeated) return 1;
   for (i = 1; i < source.count; ++i)
      if (source.words[i] != source.words[0]) return 0;
   return 1;
}
static INLINE int xe_spv_source_fits_instruction(size_t count, size_t prefix) {
   return prefix <= 65535 && count <= 65535 - prefix;
}
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
