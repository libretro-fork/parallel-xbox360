/* Single-owner descriptor indices. Zero-initialize the pool. A native command
 * allocator owns each batch until GPU completion; never copy a live batch.
 * The fixed link array is cold-allocated and initialized only as indices are
 * issued. No descriptor payload or pending-index array is ever copied. */
#ifndef XE_CORE_DESCRIPTOR_INDICES_H
#define XE_CORE_DESCRIPTOR_INDICES_H
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <retro_inline.h>
#ifndef XE_DESCRIPTOR_INDEX_ALLOC
#define XE_DESCRIPTOR_INDEX_ALLOC malloc
#define XE_DESCRIPTOR_INDEX_FREE free
#endif
#define XE_DESCRIPTOR_INDEX_NONE UINT32_C(0x7fffffff)
#define XE_DESCRIPTOR_INDEX_LIST UINT32_C(0x80000000)
typedef struct xe_descriptor_index_batch { uint32_t head, tail, count; } xe_descriptor_index_batch;
typedef struct xe_descriptor_indices {
  uint32_t *next;
  uint32_t capacity, reserved, allocated, free_head, free_count;
  /* Fixed hot stack for immediate frees. Larger/completed lists stay intrusive. */
  uint32_t cached[64], cached_count;
} xe_descriptor_indices;
static INLINE void xe_descriptor_index_batch_clear(xe_descriptor_index_batch *batch) {
  batch->head = batch->tail = XE_DESCRIPTOR_INDEX_NONE; batch->count = 0;
}
/* The tagged link domain exceeds D3D12's supported descriptor-heap capacities.
 * Reserved system slots are never issued, read, linked or recycled here. */
static INLINE int xe_descriptor_indices_init(xe_descriptor_indices *s, uint32_t capacity, uint32_t reserved) {
  uint32_t *next;
  size_t bytes;
  if (s->next || !capacity || reserved > capacity || capacity > XE_DESCRIPTOR_INDEX_NONE) return 0;
  bytes = (size_t)capacity * sizeof(*next);
  if (bytes / sizeof(*next) != (size_t)capacity) return 0;
  next = (uint32_t *)XE_DESCRIPTOR_INDEX_ALLOC(bytes);
  if (!next) return 0;
  s->next = next; s->capacity = capacity; s->reserved = s->allocated = reserved;
  s->free_head = XE_DESCRIPTOR_INDEX_NONE; s->free_count = s->cached_count = 0; return 1;
}
static INLINE uint32_t xe_descriptor_indices_available(const xe_descriptor_indices *s) {
  return s->next ? s->capacity - s->allocated + s->free_count : 0;
}
/* Internal pop: caller immediately publishes the live or pending link state. */
static INLINE uint32_t xe_descriptor_index_pop(xe_descriptor_indices *s) {
  uint32_t index;
  if (!s->next) return UINT32_MAX;
  if (s->free_count) {
    if (s->cached_count) index = s->cached[--s->cached_count];
    else { index = s->free_head; s->free_head = s->next[index] & XE_DESCRIPTOR_INDEX_NONE; }
    --s->free_count;
  } else {
    if (s->allocated == s->capacity) return UINT32_MAX;
    index = s->allocated++;
  }
  return index;
}
static INLINE uint32_t xe_descriptor_index_take(xe_descriptor_indices *s) {
  uint32_t index = xe_descriptor_index_pop(s);
  if (index != UINT32_MAX) s->next[index] = 0;
  return index;
}
/* Spill at most 64 immediate-free IDs, preserving the shared LIFO order.
 * This never walks the completed transient batch or copies descriptors. */
static INLINE void xe_descriptor_index_flush(xe_descriptor_indices *s) {
  uint32_t i;
  for (i = 0; i < s->cached_count; ++i) {
    uint32_t index = s->cached[i];
    s->next[index] = XE_DESCRIPTOR_INDEX_LIST | s->free_head;
    s->free_head = index;
  }
  s->cached_count = 0;
}
static INLINE int xe_descriptor_index_release(xe_descriptor_indices *s, uint32_t index) {
  if (!s->next || index < s->reserved || index >= s->allocated || (s->next[index] & XE_DESCRIPTOR_INDEX_LIST)) return 0;
  if (s->cached_count == 64) xe_descriptor_index_flush(s);
  s->next[index] = XE_DESCRIPTOR_INDEX_LIST;
  s->cached[s->cached_count++] = index; ++s->free_count; return 1;
}
/* Preflight the entire request against available() before issuing its indices.
 * Once preflight succeeds this path cannot allocate or fail for valid ownership. */
static INLINE uint32_t xe_descriptor_index_take_one_use(xe_descriptor_indices *s, xe_descriptor_index_batch *batch) {
  uint32_t index = xe_descriptor_index_pop(s);
  if (index == UINT32_MAX) return index;
  s->next[index] = XE_DESCRIPTOR_INDEX_LIST | (batch->count ? batch->head : XE_DESCRIPTOR_INDEX_NONE);
  if (!batch->count) batch->tail = index;
  batch->head = index; ++batch->count; return index;
}
/* O(1) metadata transfer to the existing native submission owner. No index or
 * descriptor data is copied. Refuse to overwrite another live batch. */
static INLINE int xe_descriptor_index_batch_move(xe_descriptor_index_batch *destination, xe_descriptor_index_batch *source) {
  if (destination == source || destination->count) return 0;
  destination->head = source->head; destination->tail = source->tail; destination->count = source->count;
  xe_descriptor_index_batch_clear(source); return 1;
}
/* Invoke only at the existing GPU-completed submission boundary. Flush the
 * bounded immediate-free cache before the O(1) batch splice. Reversing use
 * order while building the batch reproduces FIFO retirement -> free-LIFO order. */
static INLINE int xe_descriptor_index_batch_recycle(xe_descriptor_indices *s, xe_descriptor_index_batch *batch) {
  if (!batch->count) return 1;
  if (!s->next || batch->head < s->reserved || batch->head >= s->allocated ||
      batch->tail < s->reserved || batch->tail >= s->allocated ||
      batch->count > s->allocated - s->reserved - s->free_count) return 0;
  xe_descriptor_index_flush(s);
  s->next[batch->tail] = XE_DESCRIPTOR_INDEX_LIST | s->free_head;
  s->free_head = batch->head; s->free_count += batch->count;
  xe_descriptor_index_batch_clear(batch); return 1;
}
/* Native heap teardown only, after the caller has drained/stopped GPU use. */
static INLINE void xe_descriptor_indices_destroy(xe_descriptor_indices *s) {
  XE_DESCRIPTOR_INDEX_FREE(s->next); memset(s, 0, sizeof(*s));
}
#ifdef __cplusplus
/* Thin ownership boundary: retain the original member-destruction order so
 * later-declared texture owners may release indices before this storage dies. */
struct xe_descriptor_indices_owner : xe_descriptor_indices {
  xe_descriptor_indices_owner() : xe_descriptor_indices{} {}
  xe_descriptor_indices_owner(const xe_descriptor_indices_owner&) = delete;
  xe_descriptor_indices_owner& operator=(const xe_descriptor_indices_owner&) = delete;
  ~xe_descriptor_indices_owner() { xe_descriptor_indices_destroy(this); }
};
#endif
#endif
