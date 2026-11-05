/* Single-compiling-owner ID lookup. Entries borrow instruction records; reset
 * invalidates every entry without clearing/copying retained capacity. */
#ifndef XE_CORE_SPV_INDEX_H
#define XE_CORE_SPV_INDEX_H
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <retro_inline.h>
#ifndef XE_SPV_INDEX_REALLOC
#define XE_SPV_INDEX_REALLOC realloc
#define XE_SPV_INDEX_FREE free
#endif
typedef struct xe_spv_index {
   void **entries;
   size_t count, capacity;
} xe_spv_index;
static INLINE int xe_spv_index_reserve(xe_spv_index *index, size_t count)
{
   size_t capacity = index->capacity ? index->capacity : 64;
   const size_t limit = (size_t)-1 / sizeof(void *);
   void **entries;
   if (count <= index->capacity) return 1;
   if (count > limit) return 0;
   while (capacity < count) {
      if (capacity > limit / 2) { capacity = count; break; }
      capacity *= 2;
   }
   entries = (void **)XE_SPV_INDEX_REALLOC(index->entries, capacity * sizeof(*entries));
   if (!entries) return 0;
   index->entries = entries;
   index->capacity = capacity;
   return 1;
}
static INLINE int xe_spv_index_set(xe_spv_index *index, size_t id, void *record)
{
   if (id == (size_t)-1 || !xe_spv_index_reserve(index, id + 1)) return 0;
   if (id >= index->count) {
      /* Only holes need initialization. The mapped slot is overwritten below. */
      if (id > index->count)
         memset(index->entries + index->count, 0, (id - index->count) * sizeof(void *));
      index->count = id + 1;
   }
   index->entries[id] = record;
   return 1;
}
static INLINE void *xe_spv_index_get(const xe_spv_index *index, size_t id)
{
   return id < index->count ? index->entries[id] : NULL;
}
static INLINE void xe_spv_index_reset(xe_spv_index *index)
{
   index->count = 0;
}
static INLINE void xe_spv_index_close(xe_spv_index *index)
{
   XE_SPV_INDEX_FREE(index->entries);
   memset(index, 0, sizeof(*index));
}
#endif
