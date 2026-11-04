#ifndef XE_CORE_GPU_RETIRE_H
#define XE_CORE_GPU_RETIRE_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Owned by one command-processor thread. Nodes live in the retired resource;
 * release may free the node. Tokens must be enqueued in nondecreasing order. */
typedef struct xe_gpu_retire_node {
   struct xe_gpu_retire_node *next;
   uint64_t submission;
   void (*release)(void *);
   void *resource;
} xe_gpu_retire_node;
typedef struct xe_gpu_retire_queue {
   xe_gpu_retire_node *head, *tail;
} xe_gpu_retire_queue;
typedef struct xe_gpu_cache_gc {
   uint64_t next_frame, generation;
} xe_gpu_cache_gc;
int xe_gpu_retire_append(xe_gpu_retire_queue *queue, xe_gpu_retire_node *node,
      uint64_t submission, void (*release)(void *), void *resource);
size_t xe_gpu_retire_collect(xe_gpu_retire_queue *queue, uint64_t completed);
/* A failed collection must not advance this state. Call only after ownership
 * containers for a retirement batch have been allocated successfully. */
int xe_gpu_cache_gc_due(const xe_gpu_cache_gc *gc, uint64_t frame,
      uint64_t bytes, uint64_t budget, uint64_t generation);
void xe_gpu_cache_gc_commit(xe_gpu_cache_gc *gc, uint64_t frame,
      uint64_t generation, unsigned retain_frames);
#ifdef __cplusplus
}
#endif
#endif
