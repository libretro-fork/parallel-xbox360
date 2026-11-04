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
   unsigned cost;
} xe_gpu_retire_node;
typedef struct xe_gpu_retire_queue {
   xe_gpu_retire_node *head, *tail;
} xe_gpu_retire_queue;
enum { XE_GPU_RETIRE_FRAME_BUDGET = 128, XE_GPU_RETIRE_IMAGE_COST = 64 };
typedef struct xe_gpu_retire_budget {
   uint64_t frame;
   unsigned remaining;
   int initialized;
} xe_gpu_retire_budget;
typedef struct xe_gpu_retire_pool xe_gpu_retire_pool;
typedef struct xe_gpu_retire_slot {
   xe_gpu_retire_node retirement;
   xe_gpu_retire_pool *pool;
   size_t next;
   uint64_t handle;
   void *context;
   void (*destroy)(void *, uint64_t);
} xe_gpu_retire_slot;
struct xe_gpu_retire_pool {
   xe_gpu_retire_slot *slots;
   size_t capacity, available, free_head;
};
/* Fixed metadata storage; used only by the command-processor owner. */
int xe_gpu_retire_pool_init(xe_gpu_retire_pool *pool, xe_gpu_retire_slot *slots,
      size_t count);
int xe_gpu_retire_handle(xe_gpu_retire_queue *queue, xe_gpu_retire_pool *pool,
      uint64_t submission, uint64_t handle,
      void (*destroy)(void *, uint64_t), void *context);
typedef struct xe_gpu_cache_gc {
   uint64_t next_frame, generation;
} xe_gpu_cache_gc;
/* Borrowed candidate, valid only during the command processor's cache scan.
 * Address changes do not change resource identity or copy its contents. */
typedef struct xe_gpu_surface_choice {
   void *resource;
   uint64_t last_use;
} xe_gpu_surface_choice;
int xe_gpu_surface_recent(uint64_t frame, uint64_t last_use, unsigned frames);
void xe_gpu_surface_consider(xe_gpu_surface_choice *choice, void *resource,
      uint64_t wanted_shape, uint64_t shape, uint64_t frame,
      uint64_t last_use, int protected_content);
int xe_gpu_retire_append(xe_gpu_retire_queue *queue, xe_gpu_retire_node *node,
      uint64_t submission, void (*release)(void *), void *resource);
int xe_gpu_retire_append_cost(xe_gpu_retire_queue *queue, xe_gpu_retire_node *node,
      uint64_t submission, void (*release)(void *), void *resource, unsigned cost);
size_t xe_gpu_retire_collect(xe_gpu_retire_queue *queue, uint64_t completed);
/* Repeated completion notifications share one budget per actual guest frame.
 * A large resource makes progress once per frame even if it exceeds the budget. */
size_t xe_gpu_retire_collect_frame(xe_gpu_retire_queue *queue, uint64_t completed,
      xe_gpu_retire_budget *budget, uint64_t frame, unsigned allowance);
/* A deferred collection must not advance this state. */
int xe_gpu_cache_gc_due(const xe_gpu_cache_gc *gc, uint64_t frame,
      uint64_t bytes, uint64_t budget, uint64_t generation);
void xe_gpu_cache_gc_commit(xe_gpu_cache_gc *gc, uint64_t frame,
      uint64_t generation, unsigned retain_frames);
#ifdef __cplusplus
}
#endif
#endif
