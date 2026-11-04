/* Stack-only resolve classification; MSVC C89. Never retains source views. */
#ifndef XE_CORE_GPU_RESOLVE_SCAN_H
#define XE_CORE_GPU_RESOLVE_SCAN_H
#include <stdint.h>
enum { XE_GPU_RESOLVE_REQUIRE_NATIVE = 1, XE_GPU_RESOLVE_REQUIRE_LAYOUT = 2 };
typedef struct xe_gpu_resolve_scan {
   uint64_t tiles;
   uint32_t row_length, wanted_layout;
   unsigned required, invalid, found;
} xe_gpu_resolve_scan;
static int xe_gpu_resolve_scan_add(xe_gpu_resolve_scan *scan,
      uint32_t layout, int native_scale, uint32_t tiles)
{
   scan->found = 1;
   scan->tiles += tiles;
   if (!native_scale) scan->invalid |= XE_GPU_RESOLVE_REQUIRE_NATIVE;
   if (layout != scan->wanted_layout)
      scan->invalid |= XE_GPU_RESOLVE_REQUIRE_LAYOUT;
   return !(scan->invalid & scan->required);
}
#endif
