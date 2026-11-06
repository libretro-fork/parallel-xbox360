/* Module-owned encoded extensions. Group slots borrow payload-pool blobs.
 * Disabled names stay cached until lease reset, so re-add needs no allocation.
 * The prefix is not serialized; wire words follow it in the same allocation. */
#ifndef XE_CORE_SPV_EXTENSIONS_H
#define XE_CORE_SPV_EXTENSIONS_H
#include "core_spv_groups.h"
#include "core_spv_words.h"
#include <string.h>
typedef struct xe_spv_extension { uint32_t active; } xe_spv_extension;
#define XE_SPV_EXTENSION_GROUP_KEY UINT32_C(0x80000000)
enum { XE_SPV_EXTENSION_OPCODE = 10, XE_SPV_EXTENSION_COLD_CAPACITY = 16 };
static INLINE const uint32_t *xe_spv_extension_words(const xe_spv_extension *extension) {
   return (const uint32_t *)(extension + 1);
}
static INLINE xe_spv_word_view xe_spv_extension_view(const xe_spv_extension *extension) {
   const uint32_t *words = xe_spv_extension_words(extension);
   return xe_spv_words_view(words, words[0] >> 16);
}
static INLINE int xe_spv_extension_compare(const xe_spv_extension *extension, const char *name) {
   const uint32_t one = 1;
   const uint32_t *words = xe_spv_extension_words(extension) + 1;
   size_t i = 0;
   unsigned encoded, input;
   /* The byte view is free on supported little-endian hosts. The fallback
    * compares logical SPIR-V string bytes without a decode/copy buffer. */
   if (*(const unsigned char *)&one) return strcmp((const char *)words, name);
   for (;;) {
      encoded = (words[i / 4] >> ((i & 3) * 8)) & 255;
      input = (unsigned char)name[i];
      if (encoded != input) return encoded < input ? -1 : 1;
      if (!encoded) return 0;
      ++i;
   }
}
static INLINE size_t xe_spv_extensions_locate(xe_spv_record_view view, const char *name, int *found) {
   size_t first = 0, last = view.count;
   int comparison;
   *found = 0;
   while (first < last) {
      size_t middle = first + (last - first) / 2;
      comparison = xe_spv_extension_compare((const xe_spv_extension *)view.records[middle], name);
      if (!comparison) { *found = 1; return middle; }
      if (comparison < 0) first = middle + 1;
      else last = middle;
   }
   return first;
}
static INLINE xe_spv_extension *xe_spv_extensions_find(xe_spv_record_view view, const char *name) {
   size_t slot;
   int found;
   if (!name) return NULL;
   slot = xe_spv_extensions_locate(view, name, &found);
   if (!found) return NULL;
   return (xe_spv_extension *)view.records[slot];
}
#endif
