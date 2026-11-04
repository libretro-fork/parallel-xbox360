#include "core_pause_gate.h"
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
#include <retro_atomic.h>
#include <rthreads/retro_eventcount.h>
#include <stdlib.h>
struct xe_pause_gate {
   retro_atomic_int_t paused, stopped;
   retro_eventcount_t changed;
};
xe_pause_gate *xe_pause_gate_new(void)
{
   xe_pause_gate *gate = (xe_pause_gate*)calloc(1,sizeof(*gate));
   if (!gate) return NULL;
   if (!retro_eventcount_init(&gate->changed)) { free(gate); return NULL; }
   return gate;
}
void xe_pause_gate_set(xe_pause_gate *gate, int paused)
{
   if (!gate) return;
   retro_atomic_store_release_int(&gate->paused,paused != 0);
   if (!paused) retro_eventcount_notify(&gate->changed);
}
int xe_pause_gate_is_paused(const xe_pause_gate *gate)
{
   return gate && retro_atomic_load_acquire_int(&gate->paused);
}
int xe_pause_gate_wait(xe_pause_gate *gate)
{
   int key;
   if (!gate) return 0;
   for (;;) {
      if (retro_atomic_load_acquire_int(&gate->stopped)) return 0;
      if (!retro_atomic_load_acquire_int(&gate->paused)) return 1;
      key = retro_eventcount_prepare_wait(&gate->changed);
      if (retro_atomic_load_acquire_int(&gate->stopped) ||
            !retro_atomic_load_acquire_int(&gate->paused))
         retro_eventcount_cancel_wait(&gate->changed);
      else retro_eventcount_commit_wait(&gate->changed,key);
   }
}
void xe_pause_gate_stop(xe_pause_gate *gate)
{
   if (!gate) return;
   retro_atomic_store_release_int(&gate->stopped,1);
   retro_eventcount_notify(&gate->changed);
}
void xe_pause_gate_free(xe_pause_gate *gate)
{
   if (!gate) return;
   retro_eventcount_free(&gate->changed);
   free(gate);
}
