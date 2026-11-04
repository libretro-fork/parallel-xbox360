/* Fixed metadata for GPU block suballocation; MSVC C89, one owning thread. */
#ifndef XE_CORE_GPU_SPANS_H
#define XE_CORE_GPU_SPANS_H
#include <stdint.h>
#define XE_GPU_SPAN_NONE UINT32_MAX
#ifdef __cplusplus
extern "C" {
#endif
struct xe_gpu_span_arena;
typedef struct xe_gpu_span_node {
   uint64_t offset, size, tag;
   uint32_t prev, next, free_prev, free_next, spare;
   struct xe_gpu_span_arena *owner;
   unsigned live;
} xe_gpu_span_node;
typedef struct xe_gpu_span_pool {
   xe_gpu_span_node *nodes;
   uint32_t capacity, spare, available;
   uint64_t generation;
} xe_gpu_span_pool;
typedef struct xe_gpu_span_arena {
   xe_gpu_span_pool *pool;
   uint64_t size, used;
   uint64_t free_mask;
   uint32_t root, free_heads[64];
} xe_gpu_span_arena;
typedef struct xe_gpu_span {
   uint64_t offset, size, tag;
   uint32_t slot;
} xe_gpu_span;
/* Pool/arena must be zero-initialized. Caller storage remains alive until all
 * arenas close, and arena addresses must remain stable. No operation allocates, moves live spans or shifts arrays. */
int xe_gpu_span_pool_init(xe_gpu_span_pool *pool, xe_gpu_span_node *nodes, uint32_t capacity);
int xe_gpu_span_arena_init(xe_gpu_span_arena *arena, xe_gpu_span_pool *pool, uint64_t size);
/* Alignment must be a nonzero power of two. Failed requests change no state. */
int xe_gpu_span_take(xe_gpu_span_arena *arena, uint64_t size, uint64_t alignment, xe_gpu_span *out);
/* The caller must first complete all GPU use and destroy bound resources.
 * Cross-arena, stale, duplicate and modified-handle frees are rejected. */
int xe_gpu_span_return(xe_gpu_span_arena *arena, const xe_gpu_span *span);
/* Refuses arenas with live allocations. */
int xe_gpu_span_arena_close(xe_gpu_span_arena *arena);
#ifdef __cplusplus
}
#endif
#endif
