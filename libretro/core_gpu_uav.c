#include "core_gpu_uav.h"
void xe_gpu_uav_clear(xe_gpu_uav *s) { s->count = s->unknown = 0; }
void xe_gpu_uav_init(xe_gpu_uav *s, uint32_t resource_state) {
   xe_gpu_uav_clear(s); s->resource_state = resource_state;
}
int xe_gpu_uav_prepare(xe_gpu_uav *s, uint32_t next_state, int next_is_uav,
      uint32_t first, uint32_t length, uint32_t group_base) {
   uint32_t end;
   unsigned i;
   int barrier = 0;
   if (s->resource_state != next_state || !next_is_uav) xe_gpu_uav_clear(s);
   s->resource_state = next_state;
   if (!next_is_uav) return 0;
   if (!length || group_base > first || first >= XE_GPU_UAV_APERTURE ||
         length > XE_GPU_UAV_APERTURE - first) {
      xe_gpu_uav_clear(s); s->unknown = 1; return 1;
   }
   end = first + length;
   /* Resolution-scaled stores rearrange complete 64/128-byte guest groups.
    * Normalize relative to the actual shader binding base, not address zero:
    * different base phases can make disjoint raw extents overlap on the GPU. */
   first = group_base + ((first - group_base) & ~UINT32_C(127));
   end = group_base + ((end - group_base + 127) & ~UINT32_C(127));
   if (end > XE_GPU_UAV_APERTURE) {
      xe_gpu_uav_clear(s); s->unknown = 1; return 1;
   }
   if (s->unknown) barrier = 1;
   for (i = 0; !barrier && i < s->count; ++i)
      if (first < s->spans[i].end && s->spans[i].first < end) barrier = 1;
   if (barrier) xe_gpu_uav_clear(s);
   /* Merge touching spans without widening across gaps. Restart when a merge
    * expands the union: a prior span may now touch the expanded endpoints. */
   for (i = 0; i < s->count;) {
      if (first <= s->spans[i].end && s->spans[i].first <= end) {
         if (s->spans[i].first < first) first = s->spans[i].first;
         if (s->spans[i].end > end) end = s->spans[i].end;
         s->spans[i] = s->spans[--s->count]; i = 0;
      } else ++i;
   }
   if (s->count == XE_GPU_UAV_SPANS) {
      xe_gpu_uav_clear(s); barrier = 1;
   }
   s->spans[s->count].first = first; s->spans[s->count].end = end;
   ++s->count;
   return barrier;
}
