/* Checked native resolve destination views. Borrow bytes; never copy them.
 * Shader addressing remains u32 and adds shader_base after native tiling.
 * MSVC C89; output is published only on success. */
#ifndef XE_CORE_GPU_RESOLVE_VIEW_H
#define XE_CORE_GPU_RESOLVE_VIEW_H
#include <stdint.h>
#include "../third_party/libretro-common/include/retro_inline.h"
typedef struct xe_gpu_resolve_view {
  uint64_t offset, range;
  uint32_t shader_base;
  int persistent;
} xe_gpu_resolve_view;
/* Bound the unscaled prefix before multiplying it by the resolution area. */
static INLINE int xe_gpu_resolve_extent(uint64_t capacity, uint64_t base,
    uint64_t start, uint64_t length, uint32_t *out) {
  uint64_t prefix;
  if (!out || !length || base > start || start > capacity ||
      length > capacity-start) return 0;
  prefix = start-base+length;
  if (prefix > UINT32_MAX) return 0;
  *out = (uint32_t)prefix; return 1;
}
static INLINE int xe_gpu_resolve_view_plan(uint64_t capacity, uint64_t base,
    uint64_t start, uint64_t length, uint64_t max_range, uint64_t alignment,
    int persistent_available, xe_gpu_resolve_view *out) {
  xe_gpu_resolve_view view;
  uint64_t end, padded_end;
  if (!out || !length || !alignment || (alignment & (alignment - 1)) ||
      (base & 3) || base > start || start > capacity ||
      length > capacity - start) return 0;
  end = start + length;
  if (end - 1 > UINT32_MAX || end > UINT64_MAX - 15) return 0;
  padded_end = (end + 15) & ~UINT64_C(15);
  if (padded_end > capacity) return 0;
  view.persistent = persistent_available && capacity <= max_range;
  if (view.persistent) {
    view.offset = 0; view.range = capacity; view.shader_base = (uint32_t)base;
  } else {
    /* Preserve uint-vector alignment even when the device requires less. */
    if (alignment < 16) alignment = 16;
    view.offset = base & ~(alignment - 1);
    view.range = padded_end - view.offset;
    if (view.range > max_range) return 0;
    view.shader_base = (uint32_t)(base - view.offset);
  }
  *out = view; return 1;
}
#endif
