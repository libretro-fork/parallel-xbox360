/* Direct-resolve workgroup bounds; MSVC C89, no storage or shader changes. */
#ifndef XE_CORE_RESOLVE_DISPATCH_H
#define XE_CORE_RESOLVE_DISPATCH_H
#include <stdint.h>
/* A shader tests (origin + invocation * pixels_per_thread - offset) < size
 * with uint32 arithmetic before any texture load/store. Keep the origin and
 * every potentially-writing workgroup. Only remove complete rejected groups.
 * Preserve the old launch if the inputs would wrap the shader's arithmetic. */
static uint32_t xe_resolve_dispatch_axis(uint32_t groups, uint32_t group_pixels_log2,
      uint32_t origin, uint32_t offset_div8, uint32_t size_div8, uint32_t scale) {
   uint64_t begin, end, covered, needed;
   if (!groups || group_pixels_log2 > 31 || !scale || scale > 16 || offset_div8 > 15)
      return groups;
   begin = (uint64_t)offset_div8 * 8 * scale;
   end = begin + (uint64_t)size_div8 * 8 * scale;
   covered = (uint64_t)groups << group_pixels_log2;
   if (end > UINT32_MAX || covered > UINT32_MAX ||
         covered + origin > UINT32_MAX) return groups;
   if (!size_div8) return 0;
   if (origin >= end || (uint64_t)origin + covered <= begin) return 0;
   needed = (end - origin + (UINT64_C(1) << group_pixels_log2) - 1) >> group_pixels_log2;
   return needed < groups ? (uint32_t)needed : groups;
}
#endif
