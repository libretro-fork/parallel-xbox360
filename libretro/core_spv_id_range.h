/* Contiguous parameter IDs. Records remain owned by the typed Function and
 * indexed by its exclusive module workspace; no pointer array is duplicated. */
#ifndef XE_CORE_SPV_ID_RANGE_H
#define XE_CORE_SPV_ID_RANGE_H
#include <stddef.h>
#include <stdint.h>
#include <retro_inline.h>
typedef struct xe_spv_id_range { uint32_t first, count; } xe_spv_id_range;
static INLINE int xe_spv_id_range_set(xe_spv_id_range *range,
      uint32_t first, size_t count)
{
   if ((size_t)(uint32_t)count != count || (count && (!first || count - 1 > UINT32_MAX - first)))
      return 0;
   range->first = count ? first : 0;
   range->count = (uint32_t)count;
   return 1;
}
static INLINE uint32_t xe_spv_id_range_get(const xe_spv_id_range *range, size_t index)
{
   return index < range->count ? range->first + (uint32_t)index : 0;
}
#endif
