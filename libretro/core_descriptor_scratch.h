/* Transient synchronous API records: prepare only after the preceding borrower
 * has returned. Growth deliberately discards expired records without copying. */
#ifndef XE_CORE_DESCRIPTOR_SCRATCH_H
#define XE_CORE_DESCRIPTOR_SCRATCH_H
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <retro_inline.h>
#ifndef XE_DESCRIPTOR_ALLOC
#define XE_DESCRIPTOR_ALLOC malloc
#define XE_DESCRIPTOR_FREE free
#endif
typedef struct xe_descriptor_scratch { uint8_t *data; size_t capacity; } xe_descriptor_scratch;
typedef struct xe_descriptor_plan { size_t offsets[4], count, bytes; } xe_descriptor_plan;
static INLINE int xe_descriptor_prepare(xe_descriptor_scratch *storage,
    const size_t counts[4], size_t stride, xe_descriptor_plan *plan) {
  xe_descriptor_plan p;
  uint8_t *data;
  size_t i, capacity;
  if (!storage || !counts || !stride || !plan) return 0;
  p.count = 0;
  for (i = 0; i < 4; ++i) {
    p.offsets[i] = p.count;
    if (counts[i] > (size_t)-1 - p.count) return 0;
    p.count += counts[i];
  }
  if (p.count > (size_t)-1 / stride) return 0;
  p.bytes = p.count * stride;
  if (p.bytes > storage->capacity) {
    capacity = storage->capacity;
    if (capacity <= (size_t)-1 / 2) capacity *= 2;
    if (capacity < p.bytes) capacity = p.bytes;
    data = (uint8_t *)XE_DESCRIPTOR_ALLOC(capacity);
    if (!data) return 0;
    XE_DESCRIPTOR_FREE(storage->data);
    storage->data = data; storage->capacity = capacity;
  }
  *plan = p; return 1;
}
static INLINE void xe_descriptor_scratch_free(xe_descriptor_scratch *storage) {
  XE_DESCRIPTOR_FREE(storage->data); storage->data = NULL; storage->capacity = 0;
}
#endif
