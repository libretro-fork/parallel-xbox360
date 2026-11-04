#include "core_gpu_retire.h"
int xe_gpu_retire_append(xe_gpu_retire_queue *queue, xe_gpu_retire_node *node,
      uint64_t submission, void (*release)(void *), void *resource)
{
   if (!queue || !node || !release ||
         (queue->tail && queue->tail->submission > submission)) return 0;
   node->next = NULL;
   node->submission = submission;
   node->release = release;
   node->resource = resource;
   if (queue->tail) queue->tail->next = node;
   else queue->head = node;
   queue->tail = node;
   return 1;
}
size_t xe_gpu_retire_collect(xe_gpu_retire_queue *queue, uint64_t completed)
{
   size_t count = 0;
   xe_gpu_retire_node *node;
   void (*release)(void *);
   void *resource;
   if (!queue) return 0;
   while ((node = queue->head) != NULL && node->submission <= completed) {
      queue->head = node->next;
      if (!queue->head) queue->tail = NULL;
      release = node->release;
      resource = node->resource;
      node->next = NULL;
      release(resource);
      ++count;
   }
   return count;
}
int xe_gpu_cache_gc_due(const xe_gpu_cache_gc *gc, uint64_t frame,
      uint64_t bytes, uint64_t budget, uint64_t generation)
{
   return gc && bytes > budget && frame >= gc->next_frame &&
      generation != gc->generation;
}
void xe_gpu_cache_gc_commit(xe_gpu_cache_gc *gc, uint64_t frame,
      uint64_t generation, unsigned retain_frames)
{
   if (!gc) return;
   gc->generation = generation;
   gc->next_frame = UINT64_MAX - frame < retain_frames ?
      UINT64_MAX : frame + retain_frames;
}
