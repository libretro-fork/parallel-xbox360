/* Module-owned raw decorations. Keys borrow caller values/text only during add;
 * sorted metadata borrows immutable encoded blobs until the module lease ends. */
#ifndef XE_CORE_SPV_DECORATIONS_H
#define XE_CORE_SPV_DECORATIONS_H
#include "core_spv_groups.h"
#include "core_spv_words.h"
#include <string.h>
#define XE_SPV_DECORATION_GROUP_KEY UINT32_C(0x80000001)
enum { XE_SPV_DECORATION_COLD_CAPACITY = 128,
   XE_SPV_DECORATE = 71, XE_SPV_MEMBER_DECORATE = 72,
   XE_SPV_DECORATE_ID = 332, XE_SPV_DECORATE_STRING = 5632,
   XE_SPV_MEMBER_DECORATE_STRING = 5633,
   XE_SPV_ARRAY_STRIDE = 6, XE_SPV_MATRIX_STRIDE = 7, XE_SPV_OFFSET = 35,
   XE_SPV_RESTRICT_POINTER = 5355, XE_SPV_ALIASED_POINTER = 5356 };
typedef struct xe_spv_decoration {
   uint32_t active, id_begin, id_end;
   /* Header, target ID and remaining words follow. Only the contiguous value
    * range can carry ID flags; prefixes, strings and optional suffix are literal. */
} xe_spv_decoration;
typedef struct xe_spv_decoration_key {
   uint32_t opcode, target, prefix[2];
   size_t prefix_count;
   xe_spv_word_view values;
   int values_are_ids;
   const char *const *strings;
   size_t string_count, first_string_bytes;
   int first_string_counted;
   uint32_t suffix;
   int has_suffix;
} xe_spv_decoration_key;
static INLINE void xe_spv_decoration_key_init(xe_spv_decoration_key *key,
      uint32_t opcode, uint32_t target, uint32_t decoration) {
   memset(key, 0, sizeof(*key));
   key->opcode = opcode; key->target = target;
   key->prefix[0] = decoration; key->prefix_count = 1;
}
static INLINE const uint32_t *xe_spv_decoration_words(const xe_spv_decoration *record) {
   return (const uint32_t *)(record + 1);
}
static INLINE xe_spv_word_view xe_spv_decoration_view(const xe_spv_decoration *record) {
   const uint32_t *words = xe_spv_decoration_words(record);
   return xe_spv_words_view(words, words[0] >> 16);
}
static INLINE size_t xe_spv_decoration_string_bytes(const xe_spv_decoration_key *key, size_t i) {
   return !i && key->first_string_counted ? key->first_string_bytes : strlen(key->strings[i]);
}
static INLINE int xe_spv_decoration_measure(const xe_spv_decoration_key *key, size_t *count) {
   size_t total, i, bytes, added;
   if (key->opcode > 65535 || key->prefix_count > 2 ||
       key->values.count > 65533 - key->prefix_count ||
       (key->values.count && !key->values.words) ||
       (key->string_count && !key->strings)) return 0;
   total = 2 + key->prefix_count + key->values.count;
   /* Every string needs at least one word; bound the loop before any reads. */
   if (key->string_count > 65535 - total) return 0;
   for (i = 0; i < key->string_count; ++i) {
      if (!key->strings[i] && (i || !key->first_string_counted || key->first_string_bytes)) return 0;
      bytes = xe_spv_decoration_string_bytes(key, i);
      added = bytes / 4 + 1;
      if (added > 65535 - total) return 0;
      total += added;
   }
   if (key->has_suffix) {
      if (total == 65535) return 0;
      ++total;
   }
   *count = total;
   return 1;
}
/* Stream logical input words without encoding a staging buffer. */
typedef struct xe_spv_decoration_cursor {
   const xe_spv_decoration_key *key;
   size_t prefix, value, string, byte, bytes;
   int string_started, suffix;
} xe_spv_decoration_cursor;
static INLINE void xe_spv_decoration_cursor_init(xe_spv_decoration_cursor *cursor,
      const xe_spv_decoration_key *key) {
   memset(cursor, 0, sizeof(*cursor)); cursor->key = key;
}
static INLINE int xe_spv_decoration_next(xe_spv_decoration_cursor *cursor, uint32_t *word, int *id) {
   const xe_spv_decoration_key *key = cursor->key;
   const unsigned char *text;
   size_t i, left;
   *word = 0; *id = 0;
   if (cursor->prefix < key->prefix_count) { *word = key->prefix[cursor->prefix++]; return 1; }
   if (cursor->value < key->values.count) {
      *word = key->values.words[cursor->value++]; *id = key->values_are_ids != 0; return 1;
   }
   if (cursor->string < key->string_count) {
      if (!cursor->string_started) {
         cursor->bytes = xe_spv_decoration_string_bytes(key, cursor->string);
         cursor->byte = 0; cursor->string_started = 1;
      }
      text = (const unsigned char *)key->strings[cursor->string];
      left = cursor->bytes - cursor->byte;
      *word = 0;
      for (i = 0; i < 4 && i < left; ++i) *word |= (uint32_t)text[cursor->byte + i] << (i * 8);
      if (left < 4) { ++cursor->string; cursor->string_started = 0; }
      else cursor->byte += 4;
      return 1;
   }
   if (key->has_suffix && !cursor->suffix) { cursor->suffix = 1; *word = key->suffix; return 1; }
   return 0;
}
static INLINE int xe_spv_decoration_compare(const xe_spv_decoration *record,
      const xe_spv_decoration_key *key, size_t count) {
   const uint32_t *words = xe_spv_decoration_words(record);
   size_t i, record_count = words[0] >> 16;
   uint32_t word, opcode = words[0] & 65535;
   int id, record_id;
   xe_spv_decoration_cursor cursor;
   if (words[1] != key->target) return words[1] < key->target ? -1 : 1;
   if (opcode != key->opcode) return opcode < key->opcode ? -1 : 1;
   xe_spv_decoration_cursor_init(&cursor, key);
   for (i = 2; i < record_count && i < count; ++i) {
      xe_spv_decoration_next(&cursor, &word, &id);
      record_id = i - 1 >= record->id_begin && i - 1 < record->id_end;
      if (record_id != id) return record_id < id ? -1 : 1;
      if (words[i] != word) return words[i] < word ? -1 : 1;
   }
   return record_count == count ? 0 : record_count < count ? -1 : 1;
}
static INLINE size_t xe_spv_decorations_locate(xe_spv_record_view view,
      const xe_spv_decoration_key *key, size_t count, int *found) {
   size_t first = 0, last = view.count, middle;
   int comparison;
   *found = 0;
   while (first < last) {
      middle = first + (last - first) / 2;
      comparison = xe_spv_decoration_compare((const xe_spv_decoration *)view.records[middle], key, count);
      if (!comparison) { *found = 1; return middle; }
      if (comparison < 0) first = middle + 1;
      else last = middle;
   }
   return first;
}
static INLINE size_t xe_spv_decorations_target_begin(xe_spv_record_view view, uint32_t target) {
   size_t first = 0, last = view.count, middle;
   while (first < last) {
      middle = first + (last - first) / 2;
      if (xe_spv_decoration_words((const xe_spv_decoration *)view.records[middle])[1] < target)
         first = middle + 1;
      else last = middle;
   }
   return first;
}
static INLINE void xe_spv_decorations_remove_target(xe_spv_record_view view, uint32_t target) {
   size_t i = xe_spv_decorations_target_begin(view, target);
   xe_spv_decoration *record;
   for (; i < view.count; ++i) {
      record = (xe_spv_decoration *)view.records[i];
      if (xe_spv_decoration_words(record)[1] != target) break;
      record->active = 0;
   }
}
static INLINE uint32_t xe_spv_decorations_alignment(xe_spv_record_view view,
      uint32_t target, uint32_t member, int structure) {
   size_t i = xe_spv_decorations_target_begin(view, target), count;
   const xe_spv_decoration *record;
   const uint32_t *words;
   uint32_t alignment = 0, opcode;
   for (; i < view.count; ++i) {
      record = (const xe_spv_decoration *)view.records[i];
      words = xe_spv_decoration_words(record);
      if (words[1] != target) break;
      if (!record->active) continue;
      count = words[0] >> 16; opcode = words[0] & 65535;
      if (structure) {
         if (opcode == XE_SPV_MEMBER_DECORATE && count >= 5 && words[2] == member &&
             (words[3] == XE_SPV_OFFSET || words[3] == XE_SPV_MATRIX_STRIDE)) alignment |= words[4];
      } else if (opcode == XE_SPV_DECORATE && count >= 4 && words[2] == XE_SPV_ARRAY_STRIDE) alignment |= words[3];
   }
   return alignment;
}
static INLINE int xe_spv_decorations_has_alias(xe_spv_record_view view, uint32_t target) {
   size_t i = xe_spv_decorations_target_begin(view, target);
   const xe_spv_decoration *record;
   const uint32_t *words;
   for (; i < view.count; ++i) {
      record = (const xe_spv_decoration *)view.records[i]; words = xe_spv_decoration_words(record);
      if (words[1] != target) break;
      if (record->active && (words[0] & 65535) == XE_SPV_DECORATE && words[0] >> 16 >= 3 &&
          (words[2] == XE_SPV_RESTRICT_POINTER || words[2] == XE_SPV_ALIASED_POINTER)) return 1;
   }
   return 0;
}
#endif
