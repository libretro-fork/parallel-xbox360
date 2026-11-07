#include "core_gpu_tasks.h"
#include <queues/mpsc_stack.h>
#include <retro_atomic.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#if !defined(RETRO_ATOMIC_HAS_PTR)
#error GPU control tasks require real pointer atomics
#endif
#ifndef XE_GPU_TASK_ALLOC
#define XE_GPU_TASK_ALLOC calloc
#endif
#ifndef XE_GPU_TASK_FREE
#define XE_GPU_TASK_FREE free
#endif
#define TASK_COUNT (XE_GPU_TASK_SMALL_COUNT+XE_GPU_TASK_LARGE_COUNT)
typedef struct task_slot {
  mpsc_stack_node_t link;
  xe_gpu_task task;
  uint64_t lease;
} task_slot;
struct xe_gpu_tasks {
  mpsc_stack_t incoming;
  mpsc_stack_node_t *pending;
  retro_atomic_int_t gate;
  retro_atomic_ptr_t owner;
  retro_atomic_int_t state[TASK_COUNT];
  xe_gpu_task_wake wake;
  void *context;
  task_slot slots[TASK_COUNT];
  unsigned char small[XE_GPU_TASK_SMALL_COUNT][XE_GPU_TASK_SMALL_BYTES];
  unsigned char large[XE_GPU_TASK_LARGE_COUNT][XE_GPU_TASK_LARGE_BYTES];
};
static int enter(xe_gpu_tasks *q) {
  int old;
  do {
    old=retro_atomic_load_acquire_int(&q->gate);
    if((old&1) || old>INT_MAX-2) return 0;
  } while(!retro_atomic_cas_int(&q->gate,old,old+2));
  return 1;
}
xe_gpu_tasks *xe_gpu_tasks_new(xe_gpu_task_wake wake,void *context) {
  xe_gpu_tasks *q=(xe_gpu_tasks*)XE_GPU_TASK_ALLOC(1,sizeof(*q));
  unsigned i;
  if(!q) return NULL;
  mpsc_stack_init(&q->incoming); retro_atomic_int_init(&q->gate,0);
  retro_atomic_ptr_init(&q->owner,NULL);
  for(i=0;i<TASK_COUNT;++i) retro_atomic_int_init(&q->state[i],0);
  q->wake=wake;q->context=context;return q;
}
void xe_gpu_tasks_set_owner(xe_gpu_tasks *q,void *owner) {
  if(q) retro_atomic_store_release_ptr(&q->owner,owner);
}
void *xe_gpu_tasks_owner(xe_gpu_tasks *q) {
  return q?retro_atomic_load_acquire_ptr(&q->owner):NULL;
}
int xe_gpu_tasks_payload_valid(const void *a,size_t an,const void *b,size_t bn) {
  return !(an && !a) && !(bn && !b) && an<=XE_GPU_TASK_LARGE_BYTES &&
      bn<=XE_GPU_TASK_LARGE_BYTES-an;
}
int xe_gpu_tasks_submit(xe_gpu_tasks *q,xe_gpu_task_fn run,xe_gpu_task_fn cancel,
    void *context,const void *a,size_t an,const void *b,size_t bn) {
  size_t bytes;
  unsigned first,last,i;
  unsigned char *payload;
  task_slot *slot;
  if(!q || !run || !xe_gpu_tasks_payload_valid(a,an,b,bn)) return 0;
  bytes=an+bn;
  if(bytes>XE_GPU_TASK_LARGE_BYTES || !enter(q)) return 0;
  first=bytes<=XE_GPU_TASK_SMALL_BYTES?0:XE_GPU_TASK_SMALL_COUNT;
  last=first?TASK_COUNT:XE_GPU_TASK_SMALL_COUNT;
  for(i=first;i<last;++i) if(retro_atomic_cas_int(&q->state[i],0,1)) break;
  if(i==last) { retro_atomic_fetch_add_int(&q->gate,-2);return 0; }
  slot=&q->slots[i];
  payload=first?q->large[i-XE_GPU_TASK_SMALL_COUNT]:q->small[i];
  if(an) memcpy(payload,a,an);
  if(bn) memcpy(payload+an,b,bn);
  ++slot->lease;
  slot->task.run=run;slot->task.cancel=cancel;slot->task.context=context;
  slot->task.data=payload;slot->task.size=bytes;
#ifdef XE_GPU_TASK_TEST_HELD
  xe_gpu_task_test_held(context);
#endif
  retro_atomic_store_release_int(&q->state[i],2);
  mpsc_stack_push(&q->incoming,&slot->link);
  if(q->wake) q->wake(q->context);
  retro_atomic_fetch_add_int(&q->gate,-2);return 1;
}
int xe_gpu_tasks_empty(xe_gpu_tasks *q) {
  return !q || (!q->pending && mpsc_stack_empty(&q->incoming));
}
int xe_gpu_tasks_take(xe_gpu_tasks *q,xe_gpu_task_view *out) {
  task_slot *slot;
  unsigned i;
  if(!q || !out) return 0;
  if(!q->pending) q->pending=mpsc_stack_reverse(mpsc_stack_drain(&q->incoming));
  if(!q->pending) return 0;
  slot=(task_slot*)q->pending; q->pending=slot->link.next;
  i=(unsigned)(slot-q->slots);
  retro_atomic_store_release_int(&q->state[i],3);
  out->task=&slot->task;out->lease=slot->lease;return 1;
}
int xe_gpu_tasks_finish(xe_gpu_tasks *q,const xe_gpu_task_view *view) {
  uintptr_t base,address,relative;
  unsigned i;
  if(!q || !view || !view->task) return 0;
  base=(uintptr_t)&q->slots[0].task;address=(uintptr_t)view->task;
  if(address<base) return 0;
  relative=address-base;
  if(relative%sizeof(task_slot) || relative/sizeof(task_slot)>=TASK_COUNT) return 0;
  i=(unsigned)(relative/sizeof(task_slot));
  if(retro_atomic_load_acquire_int(&q->state[i])!=3 || q->slots[i].lease!=view->lease) return 0;
  return retro_atomic_cas_int(&q->state[i],3,0)!=0;
}
void xe_gpu_tasks_close(xe_gpu_tasks *q) {
  if(q) retro_atomic_fetch_or_int(&q->gate,1);
}
int xe_gpu_tasks_closed(xe_gpu_tasks *q) {
  return !q || (retro_atomic_load_acquire_int(&q->gate)&1);
}
int xe_gpu_tasks_closed_ready(xe_gpu_tasks *q) {
  unsigned i;
  if(!q) return 1;
  if(retro_atomic_load_acquire_int(&q->gate)!=1 || !xe_gpu_tasks_empty(q)) return 0;
  for(i=0;i<TASK_COUNT;++i) if(retro_atomic_load_acquire_int(&q->state[i])) return 0;
  return 1;
}
int xe_gpu_tasks_reopen(xe_gpu_tasks *q) {
  unsigned i;
  if(!q || retro_atomic_load_acquire_int(&q->gate)>1 || !xe_gpu_tasks_empty(q)) return 0;
  for(i=0;i<TASK_COUNT;++i) if(retro_atomic_load_acquire_int(&q->state[i])) return 0;
  retro_atomic_store_release_int(&q->gate,0);return 1;
}
void xe_gpu_tasks_cancel_all(xe_gpu_tasks *q) {
  xe_gpu_task_view view;
  if(!q) return;
  xe_gpu_tasks_close(q);
  do {
    while(xe_gpu_tasks_take(q,&view)) {
      if(view.task->cancel) view.task->cancel(view.task->context,view.task->data,view.task->size);
      xe_gpu_tasks_finish(q,&view);
    }
  } while(!xe_gpu_tasks_closed_ready(q));
}
int xe_gpu_tasks_free(xe_gpu_tasks *q) {
  if(!q) return 1;
  xe_gpu_tasks_close(q);
  if(!xe_gpu_tasks_closed_ready(q)) return 0;
  XE_GPU_TASK_FREE(q);return 1;
}
int xe_gpu_tasks_copy_parts(void *destination,size_t capacity,const void *a,size_t an,
    const void *b,size_t bn) {
  unsigned char *payload=(unsigned char*)destination;
  if(!payload || (an && !a) || (bn && !b) || an>capacity || bn>capacity-an) return 0;
  if(an) memcpy(payload,a,an);
  if(bn) memcpy(payload+an,b,bn);
  return 1;
}
