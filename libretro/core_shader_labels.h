/* Owned shader labels. BSD license; MSVC C89. Analyze on one owner, publish
 * immutable views after completion, close only after every consumer stops.
 * Low 256 labels stay inline. Higher labels use one final, bounded bitmap;
 * promotion neither copies nor moves the inline bits. No tree nodes/growth. */
#ifndef XE_CORE_SHADER_LABELS_H
#define XE_CORE_SHADER_LABELS_H
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <retro_inline.h>
#include <compat/intrinsics.h>
#ifndef XE_SHADER_LABELS_CALLOC
#define XE_SHADER_LABELS_CALLOC calloc
#define XE_SHADER_LABELS_FREE free
#endif
#define XE_SHADER_LABELS_INLINE_WORDS 4
typedef struct xe_shader_labels {
   uint64_t low[XE_SHADER_LABELS_INLINE_WORDS];
   uint64_t *high;
   size_t words, count;
} xe_shader_labels;

/* Requires a zero/closed owner. Branch destinations have 13 bits; call return
 * labels can also name the CF immediately following the last scanned pair.
 * No allocation and no writes on failure. */
static INLINE int xe_shader_labels_init(xe_shader_labels *s, size_t cf_pairs)
{
   size_t bits;
   if (!s || s->words || s->high || s->count || s->low[0] || s->low[1] ||
         s->low[2] || s->low[3] || cf_pairs > ((size_t)-1 - 1) / 2) return 0;
   bits = cf_pairs * 2 + 1;
   if (bits < 8192) bits = 8192;
   s->words = bits / 64 + ((bits & 63) != 0);
   return 1;
}
static INLINE uint64_t xe_shader_labels_word(const xe_shader_labels *s,
      size_t word)
{
   if (!s || word >= s->words) return 0;
   if (word < XE_SHADER_LABELS_INLINE_WORDS) return s->low[word];
   return s->high ? s->high[word - XE_SHADER_LABELS_INLINE_WORDS] : 0;
}
/* 1 inserted, 0 already present, -1 bounds/allocation failure. Allocation failure
 * preserves every existing label and owner field, permitting an explicit retry. */
static INLINE int xe_shader_labels_insert(xe_shader_labels *s, uint32_t label)
{
   size_t word = (size_t)label / 64, high_words;
   uint64_t bit, *slot, *high;
   if (!s || word >= s->words) return -1;
#if SIZE_MAX == UINT32_MAX
   if (label == UINT32_MAX) return -1;
#endif
   if (word < XE_SHADER_LABELS_INLINE_WORDS) slot = s->low + word;
   else {
      if (!s->high) {
         high_words = s->words - XE_SHADER_LABELS_INLINE_WORDS;
         if (high_words > (size_t)-1 / sizeof(*high)) return -1;
         high = (uint64_t*)XE_SHADER_LABELS_CALLOC(high_words, sizeof(*high));
         if (!high) return -1;
         s->high = high;
      }
      slot = s->high + word - XE_SHADER_LABELS_INLINE_WORDS;
   }
   bit = UINT64_C(1) << (63 - (label & 63));
   if (*slot & bit) return 0;
   *slot |= bit;
   ++s->count;
   return 1;
}
static INLINE int xe_shader_labels_contains(const xe_shader_labels *s,
      uint32_t label)
{
   return (xe_shader_labels_word(s, (size_t)label / 64) &
         (UINT64_C(1) << (63 - (label & 63)))) != 0;
}
/* Ascending iteration; SIZE_MAX is the end marker, never an actual uint32 label.
 * Calling at SIZE_MAX returns end, and zero words never reach the CLZ intrinsic. */
static INLINE size_t xe_shader_labels_next(const xe_shader_labels *s,
      size_t first)
{
   size_t word, limit;
   uint64_t bits;
   if (!s || !s->count || first == (size_t)-1) return (size_t)-1;
   limit = s->high ? s->words : XE_SHADER_LABELS_INLINE_WORDS;
   word = first / 64;
   if (word >= limit) return (size_t)-1;
   bits = xe_shader_labels_word(s, word) & (UINT64_MAX >> (first & 63));
   while (!bits) {
      if (++word >= limit) return (size_t)-1;
      bits = xe_shader_labels_word(s, word);
   }
   return word * 64 + compat_clz_u64(bits);
}
static INLINE int xe_shader_labels_equal(const xe_shader_labels *a,
      const xe_shader_labels *b)
{
   size_t word, count, a_words, b_words;
   if (!a || !b || a->count != b->count) return 0;
   if (!a->count) return 1;
   a_words = a->high ? a->words : XE_SHADER_LABELS_INLINE_WORDS;
   b_words = b->high ? b->words : XE_SHADER_LABELS_INLINE_WORDS;
   count = a_words > b_words ? a_words : b_words;
   for (word = 0; word < count; ++word)
      if (xe_shader_labels_word(a, word) != xe_shader_labels_word(b, word)) return 0;
   return 1;
}
static INLINE void xe_shader_labels_close(xe_shader_labels *s)
{
   size_t word;
   if (!s) return;
   XE_SHADER_LABELS_FREE(s->high);
   s->high = NULL; s->words = s->count = 0;
   for (word = 0; word < XE_SHADER_LABELS_INLINE_WORDS; ++word) s->low[word] = 0;
}
#endif
