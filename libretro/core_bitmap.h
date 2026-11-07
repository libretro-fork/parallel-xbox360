/* Copyright 2015 Ben Vanik. All rights reserved.
 * Released under the BSD license - see LICENSE in the repository root. */
/* Single-owner cold resize/close. Concurrent acquire/release are lock-free;
 * resize and reset require quiescent users. A set bit is a free slot. */
#ifndef XE_CORE_BITMAP_H
#define XE_CORE_BITMAP_H
#include <stddef.h>
#include <retro_atomic.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xe_bitmap {
   retro_atomic_64_t *words;
   size_t count, capacity;
} xe_bitmap;
int xe_bitmap_resize(xe_bitmap *map, size_t bits);
void xe_bitmap_reset(xe_bitmap *map);
size_t xe_bitmap_acquire(xe_bitmap *map, int from_back);
int xe_bitmap_release(xe_bitmap *map, size_t index);
/* Quiescent native-word state I/O. Exact byte count; no allocation. Input and
 * output may be unaligned but must not overlap atomic storage. Invalid ranges
 * reject before mutation; these are not concurrent whole-map transactions. */
int xe_bitmap_capture(const xe_bitmap *map, void *out, size_t bytes);
int xe_bitmap_restore(xe_bitmap *map, const void *input, size_t bytes);
void xe_bitmap_close(xe_bitmap *map);
#ifdef __cplusplus
}
#endif
#endif
