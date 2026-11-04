#include "core_vblank.h"
#include <limits.h>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
#include <retro_atomic.h>
#include <rthreads/retro_eventcount.h>
#include <stdlib.h>
struct xe_vblank_queue {
   retro_atomic_int_t pending, stopped;
   retro_eventcount_t ready;
};
xe_vblank_queue *xe_vblank_queue_new(void)
{
   xe_vblank_queue *queue=(xe_vblank_queue*)calloc(1,sizeof(*queue));
   if(!queue) return NULL;
   if(!retro_eventcount_init(&queue->ready)) { free(queue); return NULL; }
   return queue;
}
int xe_vblank_queue_request(xe_vblank_queue *queue)
{
   int pending;
   if(!queue || retro_atomic_load_acquire_int(&queue->stopped)) return 0;
   do {
      pending=retro_atomic_load_relaxed_int(&queue->pending);
      if(pending==INT_MAX) return 0;
   } while(!retro_atomic_cas_int(&queue->pending,pending,pending+1));
   retro_eventcount_notify(&queue->ready);
   return 1;
}
int xe_vblank_queue_take(xe_vblank_queue *queue)
{
   int pending,key;
   if(!queue) return 0;
   for(;;) {
      if(retro_atomic_load_acquire_int(&queue->stopped)) return 0;
      pending=retro_atomic_load_acquire_int(&queue->pending);
      if(pending && retro_atomic_cas_int(&queue->pending,pending,pending-1)) return 1;
      key=retro_eventcount_prepare_wait(&queue->ready);
      if(retro_atomic_load_acquire_int(&queue->pending) ||
            retro_atomic_load_acquire_int(&queue->stopped))
         retro_eventcount_cancel_wait(&queue->ready);
      else
         retro_eventcount_commit_wait(&queue->ready,key);
   }
}
void xe_vblank_queue_stop(xe_vblank_queue *queue)
{
   if(!queue) return;
   retro_atomic_store_release_int(&queue->stopped,1);
   retro_eventcount_notify(&queue->ready);
}
void xe_vblank_queue_free(xe_vblank_queue *queue)
{
   if(!queue) return;
   retro_eventcount_free(&queue->ready);
   free(queue);
}