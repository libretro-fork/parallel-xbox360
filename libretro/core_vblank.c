#include "core_vblank.h"
#include "core_pause_gate.h"
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
   xe_pause_gate *pause;
};
xe_vblank_queue *xe_vblank_queue_new(void)
{
   xe_vblank_queue *queue=(xe_vblank_queue*)calloc(1,sizeof(*queue));
   if(!queue) return NULL;
   if(!retro_eventcount_init(&queue->ready)) { free(queue); return NULL; }
   queue->pause=xe_pause_gate_new();
   if(!queue->pause) {retro_eventcount_free(&queue->ready);free(queue);return NULL;}
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
      if(!xe_pause_gate_wait_index(queue->pause,0)) return 0;
      if(retro_atomic_load_acquire_int(&queue->stopped)) return 0;
      pending=retro_atomic_load_acquire_int(&queue->pending);
      if(pending && retro_atomic_cas_int(&queue->pending,pending,pending-1)) return 1;
      key=retro_eventcount_prepare_wait(&queue->ready);
      if(retro_atomic_load_acquire_int(&queue->pending) ||
            retro_atomic_load_acquire_int(&queue->stopped) ||
            xe_pause_gate_is_paused(queue->pause))
         retro_eventcount_cancel_wait(&queue->ready);
      else
         retro_eventcount_commit_wait(&queue->ready,key);
   }
}
int xe_vblank_queue_is_paused(const xe_vblank_queue *queue) {
   return queue && xe_pause_gate_is_paused(queue->pause);
}
void xe_vblank_queue_resume(xe_vblank_queue *queue) {
   if(queue) xe_pause_gate_set(queue->pause,0);
}
int xe_vblank_queue_pause(xe_vblank_queue *queue,int64_t timeout_us) {
   int was_paused;
   if(!queue || timeout_us<0 || retro_atomic_load_acquire_int(&queue->stopped)) return 0;
   was_paused=xe_vblank_queue_is_paused(queue);
   xe_pause_gate_set(queue->pause,1);
   retro_eventcount_notify(&queue->ready);
   if(xe_pause_gate_wait_workers(queue->pause,1,timeout_us)) return 1;
   if(!was_paused) xe_vblank_queue_resume(queue);
   return 0;
}
void xe_vblank_queue_stop(xe_vblank_queue *queue)
{
   if(!queue) return;
   retro_atomic_store_release_int(&queue->stopped,1);
   xe_pause_gate_stop(queue->pause);
   retro_eventcount_notify(&queue->ready);
}
void xe_vblank_queue_free(xe_vblank_queue *queue)
{
   if(!queue) return;
   xe_pause_gate_free(queue->pause);
   retro_eventcount_free(&queue->ready);
   free(queue);
}