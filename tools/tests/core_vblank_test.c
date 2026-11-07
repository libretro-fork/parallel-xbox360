/* Frontend-only vblank credits and shutdown wakeups. MSVC C89. */
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include "libretro/core_vblank.h"
#include "libretro/core_state_budget.h"
#include <features/features_cpu.h>
#include <rthreads/rthreads.h>
#include <retro_atomic.h>
#include <rthreads/retro_eventcount.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"FAIL line %u\n",(unsigned)__LINE__); exit(1); } } while(0)
struct fixture { xe_vblank_queue *queue; retro_eventcount_t done; retro_atomic_int_t count,hold,entered,release; };
static void worker(void *opaque)
{
   struct fixture *state=(struct fixture*)opaque;
   while(xe_vblank_queue_take(state->queue)) {
      if(retro_atomic_load_acquire_int(&state->hold)) {
         retro_atomic_store_release_int(&state->entered,1);
         retro_eventcount_notify(&state->done);
         while(!retro_atomic_load_acquire_int(&state->release)) {}
      }
      retro_atomic_fetch_add_int(&state->count,1);
      retro_eventcount_notify(&state->done);
   }
}
static void await_value(struct fixture *state,const retro_atomic_int_t *value,int target) {
   xe_state_budget budget;
   int key;
   int64_t left;
   CHECK(xe_state_budget_begin(&budget,cpu_features_get_time_usec(),2000000));
   while(retro_atomic_load_acquire_int(value)!=target) {
      left=xe_state_budget_left(&budget,cpu_features_get_time_usec());CHECK(left>0);
      key=retro_eventcount_prepare_wait(&state->done);
      if(retro_atomic_load_acquire_int(value)==target) retro_eventcount_cancel_wait(&state->done);
      else retro_eventcount_commit_wait_timeout(&state->done,key,left);
   }
}
int main(void)
{
   struct fixture state;
   sthread_t *thread;
   unsigned round,i;
   CHECK(!xe_vblank_queue_is_paused(NULL));
   CHECK(!xe_vblank_queue_pause(NULL,0));
   xe_vblank_queue_resume(NULL);
   CHECK(!xe_vblank_queue_request(NULL) && !xe_vblank_queue_take(NULL));
   xe_vblank_queue_stop(NULL);
   xe_vblank_queue_free(NULL);
   for(round=0;round<256;++round) {
      state.queue=xe_vblank_queue_new(); CHECK(state.queue);
      retro_atomic_int_init(&state.count,0);
      retro_atomic_int_init(&state.hold,0);
      retro_atomic_int_init(&state.entered,0);
      retro_atomic_int_init(&state.release,0);
      CHECK(retro_eventcount_init(&state.done));
      thread=sthread_create(worker,&state); CHECK(thread);
      CHECK(retro_atomic_load_acquire_int(&state.count)==0);
      CHECK(!xe_vblank_queue_pause(state.queue,-1));
      if(round==0) {
         retro_atomic_store_release_int(&state.hold,1);
         CHECK(xe_vblank_queue_request(state.queue));await_value(&state,&state.entered,1);
         CHECK(!xe_vblank_queue_pause(state.queue,0));
         CHECK(!xe_vblank_queue_is_paused(state.queue));
         retro_atomic_store_release_int(&state.release,1);await_value(&state,&state.count,1);
         retro_atomic_store_release_int(&state.hold,0);
      }
      if(!(round&1)) {
      CHECK(xe_vblank_queue_pause(state.queue,2000000));
      CHECK(xe_vblank_queue_is_paused(state.queue));
      CHECK(xe_vblank_queue_pause(state.queue,0));
      CHECK(!xe_vblank_queue_pause(state.queue,-1) && xe_vblank_queue_is_paused(state.queue));
      if(round==0) {
         for(i=0;i<20000;++i) CHECK(xe_vblank_queue_request(state.queue));
         CHECK(retro_atomic_load_acquire_int(&state.count)==1);
         xe_vblank_queue_resume(state.queue);await_value(&state,&state.count,20001);
         CHECK(xe_vblank_queue_pause(state.queue,2000000));
      }
         if(round) for(i=0;i<17;++i) CHECK(xe_vblank_queue_request(state.queue));
      }
      xe_vblank_queue_stop(state.queue);
      sthread_join(thread);
      CHECK(retro_atomic_load_acquire_int(&state.count)==(round ? 0 : 20001));
      CHECK(!xe_vblank_queue_request(state.queue));
      CHECK(!xe_vblank_queue_take(state.queue));
      retro_eventcount_free(&state.done);
      xe_vblank_queue_free(state.queue);
   }
   puts("Vblank: 20001 delivered credits, active callback timeout, paused credits retained, 128 paused and 128 idle stop/join races: PASS");
   return 0;
}