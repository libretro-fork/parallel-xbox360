#include "core_gpu_retire.h"
int xe_gpu_surface_recent(uint64_t frame, uint64_t last_use, unsigned frames)
{
   return frames && (last_use > frame || frame - last_use < frames);
}
void xe_gpu_surface_consider(xe_gpu_surface_choice *choice, void *resource,
      uint64_t wanted_shape, uint64_t shape, uint64_t frame,
      uint64_t last_use, int protected_content)
{
   if (!choice || !resource || protected_content || wanted_shape != shape ||
         xe_gpu_surface_recent(frame, last_use, 4)) return;
   if (!choice->resource || last_use < choice->last_use) {
      choice->resource = resource;
      choice->last_use = last_use;
   }
}
int xe_gpu_retire_append(xe_gpu_retire_queue *queue, xe_gpu_retire_node *node,
      uint64_t submission, void (*release)(void *), void *resource)
{
   return xe_gpu_retire_append_cost(queue,node,submission,release,resource,1);
}
int xe_gpu_retire_append_cost(xe_gpu_retire_queue *queue, xe_gpu_retire_node *node,
      uint64_t submission, void (*release)(void *), void *resource, unsigned cost)
{
   if (!queue || !node || !release || !cost ||
         (queue->tail && queue->tail->submission > submission)) return 0;
   node->next = NULL;
   node->submission = submission;
   node->release = release;
   node->resource = resource;
   node->cost = cost;
   if (queue->tail) queue->tail->next = node;
   else queue->head = node;
   queue->tail = node;
   return 1;
}
static void release_front(xe_gpu_retire_queue *queue)
{
   xe_gpu_retire_node *node=queue->head;
   void (*release)(void *)=node->release;
   void *resource=node->resource;
   queue->head=node->next;
   if (!queue->head) queue->tail=NULL;
   node->next=NULL;
   release(resource);
}
size_t xe_gpu_retire_collect(xe_gpu_retire_queue *queue, uint64_t completed)
{
   size_t count = 0;
   if (!queue) return 0;
   while (queue->head && queue->head->submission <= completed) {
      release_front(queue);
      ++count;
   }
   return count;
}
size_t xe_gpu_retire_collect_frame(xe_gpu_retire_queue *queue, uint64_t completed,
      xe_gpu_retire_budget *budget, uint64_t frame, unsigned allowance)
{
   size_t count=0;
   unsigned cost;
   if (!queue || !budget || !allowance) return 0;
   if (!budget->initialized || budget->frame!=frame) {
      budget->frame=frame; budget->remaining=allowance; budget->initialized=1;
   }
   while (queue->head && queue->head->submission<=completed && budget->remaining) {
      cost=queue->head->cost;
      if (cost>budget->remaining && budget->remaining!=allowance) break;
      budget->remaining=cost>=budget->remaining?0:budget->remaining-cost;
      release_front(queue);
      ++count;
   }
   return count;
}
int xe_gpu_retire_pool_init(xe_gpu_retire_pool *pool, xe_gpu_retire_slot *slots,
      size_t count)
{
   size_t i;
   if (!pool || !slots || !count) return 0;
   pool->slots=slots; pool->capacity=count; pool->available=count; pool->free_head=0;
   for (i=0;i<count;++i) { slots[i].pool=pool; slots[i].next=i+1; }
   return 1;
}
static void free_slot(xe_gpu_retire_slot *slot)
{
   xe_gpu_retire_pool *pool=slot->pool;
   slot->next=pool->free_head;
   pool->free_head=(size_t)(slot-pool->slots);
   ++pool->available;
}
static void release_handle(void *opaque)
{
   xe_gpu_retire_slot *slot=(xe_gpu_retire_slot *)opaque;
   slot->destroy(slot->context,slot->handle);
   free_slot(slot);
}
int xe_gpu_retire_handle(xe_gpu_retire_queue *queue, xe_gpu_retire_pool *pool,
      uint64_t submission, uint64_t handle,
      void (*destroy)(void *, uint64_t), void *context)
{
   xe_gpu_retire_slot *slot;
   if (!pool || !destroy || !pool->available) return 0;
   slot=&pool->slots[pool->free_head];
   pool->free_head=slot->next; --pool->available;
   slot->handle=handle; slot->context=context; slot->destroy=destroy;
   if (!xe_gpu_retire_append(queue,&slot->retirement,submission,release_handle,slot)) {
      free_slot(slot); return 0;
   }
   return 1;
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
