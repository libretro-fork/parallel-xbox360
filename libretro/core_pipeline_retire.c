#include "core_pipeline_retire.h"
#include <queues/mpsc_stack.h>
#include <stdlib.h>

#ifndef RETRO_ATOMIC_HAS_PTR
#error Pipeline retirement requires atomic pointer operations
#endif

struct xe_pipeline_retire_slot {
   mpsc_stack_node_t link;
   struct xe_pipeline_retire_pool *pool;
   uint64_t handle, submission;
};
struct xe_pipeline_retire_pool {
   mpsc_stack_t ready;
   /* Keep creator publication off the GPU owner's bookkeeping cache lines. */
   unsigned char ready_padding[128];
   xe_pipeline_retire_slot *slots, *free_head;
   mpsc_stack_node_t *pending;
   size_t available;
};
xe_pipeline_retire_pool *xe_pipeline_retire_new(size_t count)
{
   xe_pipeline_retire_pool *pool;
   size_t i;
   if (!count || count > (size_t)-1 / sizeof(xe_pipeline_retire_slot)) return NULL;
   pool = (xe_pipeline_retire_pool *)calloc(1, sizeof(*pool));
   if (!pool) return NULL;
   pool->slots = (xe_pipeline_retire_slot *)calloc(count, sizeof(*pool->slots));
   if (!pool->slots) { free(pool); return NULL; }
   mpsc_stack_init(&pool->ready);
   pool->available = count;
   for (i = 0; i < count; ++i) {
      pool->slots[i].pool = pool;
      pool->slots[i].link.next = (mpsc_stack_node_t *)pool->free_head;
      pool->free_head = &pool->slots[i];
   }
   return pool;
}
size_t xe_pipeline_retire_available(const xe_pipeline_retire_pool *pool)
{
   return pool ? pool->available : 0;
}
xe_pipeline_retire_slot *xe_pipeline_retire_reserve(xe_pipeline_retire_pool *pool)
{
   xe_pipeline_retire_slot *slot;
   if (!pool || !pool->free_head) return NULL;
   slot = pool->free_head;
   pool->free_head = (xe_pipeline_retire_slot *)slot->link.next;
   --pool->available;
   slot->link.next = NULL;
   slot->handle = slot->submission = 0;
   return slot;
}
void xe_pipeline_retire_cancel(xe_pipeline_retire_slot *slot)
{
   xe_pipeline_retire_pool *pool;
   if (!slot) return;
   pool = slot->pool;
   slot->link.next = (mpsc_stack_node_t *)pool->free_head;
   pool->free_head = slot;
   ++pool->available;
}
void xe_pipeline_retire_publish(xe_pipeline_retire_slot *slot,
      uint64_t handle, uint64_t submission)
{
   if (!slot) return;
   slot->handle = handle;
   slot->submission = submission;
   mpsc_stack_push(&slot->pool->ready, &slot->link);
}
int xe_pipeline_retire_transfer(xe_pipeline_retire_slot **owned,
      uint64_t handle, uint64_t submission)
{
   xe_pipeline_retire_slot *slot;
   if (!owned || !*owned || !handle) return 0;
   slot = *owned;
   *owned = NULL;
   xe_pipeline_retire_publish(slot, handle, submission);
   return 1;
}
size_t xe_pipeline_retire_collect(xe_pipeline_retire_pool *pool,
      uint64_t completed, size_t budget, xe_pipeline_retire_destroy destroy,
      void *opaque)
{
   xe_pipeline_retire_slot *slot;
   mpsc_stack_node_t *ready, *next, **link;
   size_t released = 0;
   if (!pool) return 0;
   ready = mpsc_stack_empty(&pool->ready) ? NULL : mpsc_stack_drain(&pool->ready);
   while (ready) {
      next = ready->next;
      slot = (xe_pipeline_retire_slot *)ready;
      slot->link.next = pool->pending;
      pool->pending = &slot->link;
      ready = next;
   }
   link = &pool->pending;
   while (*link != NULL) {
      slot = (xe_pipeline_retire_slot *)*link;
      if (slot->handle && (slot->submission > completed || !budget || !destroy)) {
         link = &slot->link.next;
         continue;
      }
      *link = slot->link.next;
      if (slot->handle) {
         destroy(opaque, slot->handle);
         --budget;
         ++released;
      }
      xe_pipeline_retire_cancel(slot);
   }
   return released;
}
int xe_pipeline_retire_free(xe_pipeline_retire_pool *pool,
      xe_pipeline_retire_destroy destroy, void *opaque)
{
   if (!pool) return 1;
   xe_pipeline_retire_collect(pool, UINT64_MAX, (size_t)-1, destroy, opaque);
   /* A missing destructor cannot release transferred native ownership. */
   if (pool->pending) return 0;
   free(pool->slots);
   free(pool);
   return 1;
}
