/* C89: resume/stop races with all six guest CPU workers, without polling. */
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include "libretro/core_pause_gate.h"
#include <retro_atomic.h>
#include <rthreads/retro_eventcount.h>
#include <rthreads/rthreads.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %u\n",(unsigned)__LINE__); exit(1); } } while (0)
#define WORKERS 6
struct state {
   xe_pause_gate *gate;
   retro_atomic_int_t ready;
   retro_eventcount_t changed;
   int results[WORKERS];
};
struct argument { struct state *state; unsigned index; };
static void worker(void *opaque)
{
   struct argument *argument = (struct argument*)opaque;
   struct state *state = argument->state;
   retro_atomic_fetch_add_int(&state->ready,1);
   retro_eventcount_notify(&state->changed);
   state->results[argument->index] = xe_pause_gate_wait(state->gate);
}
int main(void)
{
   struct state state;
   struct argument arguments[WORKERS];
   sthread_t *threads[WORKERS];
   unsigned round, i;
   int key;
   CHECK(!xe_pause_gate_wait(NULL));
   CHECK(!xe_pause_gate_is_paused(NULL));
   xe_pause_gate_set(NULL,0);
   xe_pause_gate_stop(NULL);
   xe_pause_gate_free(NULL);
   for (round = 0; round < 192; ++round) {
      state.gate = xe_pause_gate_new();
      CHECK(state.gate);
      CHECK(xe_pause_gate_wait(state.gate));
      CHECK(!xe_pause_gate_is_paused(state.gate));
      CHECK(retro_eventcount_init(&state.changed));
      retro_atomic_store_relaxed_int(&state.ready,0);
      xe_pause_gate_set(state.gate,1);
      CHECK(xe_pause_gate_is_paused(state.gate));
      for (i = 0; i < WORKERS; ++i) {
         arguments[i].state = &state;
         arguments[i].index = i;
         state.results[i] = -1;
         threads[i] = sthread_create(worker,&arguments[i]);
         CHECK(threads[i]);
      }
      while (retro_atomic_load_acquire_int(&state.ready) != WORKERS) {
         key = retro_eventcount_prepare_wait(&state.changed);
         if (retro_atomic_load_acquire_int(&state.ready) == WORKERS)
            retro_eventcount_cancel_wait(&state.changed);
         else retro_eventcount_commit_wait(&state.changed,key);
      }
      if (round % 3 != 1) xe_pause_gate_set(state.gate,0);
      if (round % 3 != 0) xe_pause_gate_stop(state.gate);
      for (i = 0; i < WORKERS; ++i) {
         sthread_join(threads[i]);
         CHECK(state.results[i] == 0 || state.results[i] == 1);
         if (round % 3 == 0) CHECK(state.results[i] == 1);
         if (round % 3 == 1) CHECK(state.results[i] == 0);
      }
      xe_pause_gate_stop(state.gate);
      CHECK(!xe_pause_gate_wait(state.gate));
      xe_pause_gate_set(state.gate,0);
      CHECK(!xe_pause_gate_wait(state.gate));
      xe_pause_gate_free(state.gate);
      retro_eventcount_free(&state.changed);
   }
   puts("PASS: 1152 workers, resume/stop wake races, shutdown remains terminal");
   return 0;
}
