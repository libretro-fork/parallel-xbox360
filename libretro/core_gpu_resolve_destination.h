/* Destination-only residency preparation. No dispatch or pixel movement here.
 * A scaled destination must never request/upload the native aperture, or fall
 * back to it after preparation fails. MSVC C89; borrowed native adapters. */
#ifndef XE_CORE_GPU_RESOLVE_DESTINATION_H
#define XE_CORE_GPU_RESOLVE_DESTINATION_H
#include "core_gpu_resolve_view.h"
typedef struct xe_gpu_resolve_destination_ops {
  int (*native_range)(void *, uint32_t, uint32_t);
  int (*scaled_prepare)(void *, uint32_t, uint32_t, uint32_t, uint32_t);
} xe_gpu_resolve_destination_ops;
static INLINE int xe_gpu_resolve_destination_prepare(
    const xe_gpu_resolve_destination_ops *ops, void *context, uint64_t capacity,
    uint32_t base, uint32_t start, uint32_t length, int scaled,
    uint32_t *prefix_out) {
  uint32_t prefix;
  if (!ops || !prefix_out ||
      !xe_gpu_resolve_extent(capacity, base, start, length, &prefix)) return 0;
  if (scaled) {
    if (!ops->scaled_prepare ||
        !ops->scaled_prepare(context, base, start, length, prefix)) return 0;
  } else if (!ops->native_range ||
      !ops->native_range(context, start, length)) return 0;
  *prefix_out = prefix;
  return 1;
}
#endif
