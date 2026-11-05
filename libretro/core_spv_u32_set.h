/* Exclusive sorted metadata storage. Zero and every other uint32 value are
 * ordinary keys. Borrowed views expire on insertion/reset/close. */
#ifndef XE_CORE_SPV_U32_SET_H
#define XE_CORE_SPV_U32_SET_H
#include "core_spv_words.h"
#include <string.h>
typedef struct xe_spv_u32_set {
   uint32_t *values;
   size_t count, capacity;
} xe_spv_u32_set;
static INLINE size_t xe_spv_u32_lower_bound(const xe_spv_u32_set *set, uint32_t value)
{
   size_t first = 0, last = set->count;
   while (first < last) {
      size_t middle = first + (last - first) / 2;
      if (set->values[middle] < value) first = middle + 1;
      else last = middle;
   }
   return first;
}
static INLINE int xe_spv_u32_set_contains(const xe_spv_u32_set *set, uint32_t value)
{
   size_t slot = xe_spv_u32_lower_bound(set, value);
   return slot < set->count && set->values[slot] == value;
}
static INLINE int xe_spv_u32_set_reserve(xe_spv_u32_set *set, size_t count,
      void *(*allocate)(size_t), void (*release)(void *))
{
   size_t capacity = set->capacity ? set->capacity : 64;
   size_t limit = (size_t)-1 / sizeof(*set->values);
   uint32_t *values;
   if (count <= set->capacity) return 1;
   if (count > limit) return 0;
   while (capacity < count) {
      if (capacity > limit / 2) { capacity = count; break; }
      capacity *= 2;
   }
   values = (uint32_t *)allocate(capacity * sizeof(*values));
   if (!values) return 0;
   if (set->count) memcpy(values, set->values, set->count * sizeof(*values));
   release(set->values);
   set->values = values;
   set->capacity = capacity;
   return 1;
}
static INLINE int xe_spv_u32_set_insert(xe_spv_u32_set *set, uint32_t value,
      void *(*allocate)(size_t), void (*release)(void *))
{
   size_t slot = xe_spv_u32_lower_bound(set, value);
   if (slot < set->count && set->values[slot] == value) return 1;
   if (set->count == (size_t)-1 || !xe_spv_u32_set_reserve(set, set->count + 1, allocate, release)) return 0;
   if (slot < set->count) memmove(set->values + slot + 1, set->values + slot,
         (set->count - slot) * sizeof(*set->values));
   set->values[slot] = value;
   ++set->count;
   return 1;
}
static INLINE xe_spv_word_view xe_spv_u32_set_view(const xe_spv_u32_set *set)
{
   return xe_spv_words_view(set->values, set->count);
}
static INLINE void xe_spv_u32_set_reset(xe_spv_u32_set *set) { set->count = 0; }
static INLINE void xe_spv_u32_set_close(xe_spv_u32_set *set, void (*release)(void *))
{
   release(set->values);
   memset(set, 0, sizeof(*set));
}
#endif
