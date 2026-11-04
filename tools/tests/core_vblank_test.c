/* Frontend-only vblank credits and shutdown wakeups. MSVC C89. */
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include "libretro/core_vblank.h"
#include <rthreads/rthreads.h>
#include <retro_atomic.h>
#include <rthreads/retro_eventcount.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"FAIL line %u\n",(unsigned)__LINE__); exit(1); } } while(0)
struct fixture { xe_vblank_queue *queue; retro_eventcount_t done; retro_atomic_int_t count; };
static void worker(void *opaque)
{
   struct fixture *state=(struct fixture*)opaque;
   while(xe_vblank_queue_take(state->queue)) {
      retro_atomic_fetch_add_int(&state->count,1);
      retro_eventcount_notify(&state->done);
   }
}
int main(void)
{
   struct fixture state;
   sthread_t *thread;
   unsigned round,i;
   int key;
   CHECK(!xe_vblank_queue_request(NULL) && !xe_vblank_queue_take(NULL));
   xe_vblank_queue_stop(NULL);
   xe_vblank_queue_free(NULL);
   for(round=0;round<128;++round) {
      state.queue=xe_vblank_queue_new(); CHECK(state.queue);
      retro_atomic_store_relaxed_int(&state.count,0);
      CHECK(retro_eventcount_init(&state.done));
      thread=sthread_create(worker,&state); CHECK(thread);
      CHECK(retro_atomic_load_acquire_int(&state.count)==0);
      if(round==0) {
         for(i=0;i<20000;++i) CHECK(xe_vblank_queue_request(state.queue));
         while(retro_atomic_load_acquire_int(&state.count)!=20000) {
            key=retro_eventcount_prepare_wait(&state.done);
            if(retro_atomic_load_acquire_int(&state.count)==20000)
               retro_eventcount_cancel_wait(&state.done);
            else retro_eventcount_commit_wait(&state.done,key);
         }
      }
      xe_vblank_queue_stop(state.queue);
      sthread_join(thread);
      CHECK(retro_atomic_load_acquire_int(&state.count)==(round ? 0 : 20000));
      CHECK(!xe_vblank_queue_request(state.queue));
      CHECK(!xe_vblank_queue_take(state.queue));
      retro_eventcount_free(&state.done);
      xe_vblank_queue_free(state.queue);
   }
   puts("Vblank: 20000 frontend requests, no autonomous ticks, 128 stop/join races: PASS");
   return 0;
}