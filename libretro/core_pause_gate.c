#include "core_pause_gate.h"
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
#include <retro_atomic.h>
#include <rthreads/retro_eventcount.h>
#include <features/features_cpu.h>
#include <stdlib.h>
struct xe_pause_gate {
   retro_atomic_int_t paused, stopped;
   retro_atomic_int_t generation, acknowledged[6], pending;
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
   if (paused && !retro_atomic_load_acquire_int(&gate->paused))
      retro_atomic_fetch_add_int(&gate->generation, 1);
   retro_atomic_store_release_int(&gate->paused,paused != 0);
   if (!paused) retro_eventcount_notify(&gate->changed);
}
int xe_pause_gate_is_paused(const xe_pause_gate *gate)
{
   return gate && retro_atomic_load_acquire_int(&gate->paused);
}
int xe_pause_gate_generation(const xe_pause_gate *gate)
{
   return gate ? retro_atomic_load_acquire_int(&gate->generation) : 0;
}
static int wait_index(xe_pause_gate *gate, unsigned index,
      int expected_generation, int match_generation)
{
   int key, generation;
   if (!gate) return 0;
   for (;;) {
      if (retro_atomic_load_acquire_int(&gate->stopped)) return 0;
      if (!retro_atomic_load_acquire_int(&gate->paused)) return 1;
      if (match_generation && xe_pause_gate_generation(gate) != expected_generation)
         return 1;
      if (index < 6) {
         generation = retro_atomic_load_acquire_int(&gate->generation);
         if (match_generation && generation != expected_generation) return 1;
         if (retro_atomic_load_relaxed_int(&gate->acknowledged[index]) != generation) {
            retro_atomic_store_release_int(&gate->acknowledged[index], generation);
            retro_eventcount_notify(&gate->changed);
         }
      }
      key = retro_eventcount_prepare_wait(&gate->changed);
      if (retro_atomic_load_acquire_int(&gate->stopped) ||
            !retro_atomic_load_acquire_int(&gate->paused) ||
            (match_generation && xe_pause_gate_generation(gate) != expected_generation) ||
            (index < 6 && retro_atomic_load_acquire_int(&gate->generation) !=
               retro_atomic_load_relaxed_int(&gate->acknowledged[index])))
         retro_eventcount_cancel_wait(&gate->changed);
      else retro_eventcount_commit_wait(&gate->changed,key);
   }
}
int xe_pause_gate_wait_index(xe_pause_gate *gate, unsigned index)
{
   return wait_index(gate, index, 0, 0);
}
int xe_pause_gate_wait_generation(xe_pause_gate *gate, unsigned index,
      int generation)
{
   return wait_index(gate, index, generation, 1);
}
int xe_pause_gate_wait(xe_pause_gate *gate)
{
   return xe_pause_gate_wait_index(gate, 6);
}
static int workers_acknowledged(const xe_pause_gate *gate, unsigned mask)
{
   unsigned i;
   int generation = retro_atomic_load_acquire_int(&gate->generation);
   for (i = 0; i < 6; ++i)
      if ((mask & (1u << i)) &&
            retro_atomic_load_acquire_int(&gate->acknowledged[i]) != generation)
         return 0;
   return retro_atomic_load_acquire_int(&gate->pending) == 0;
}
void xe_pause_gate_work_begin(xe_pause_gate *gate)
{
   if (gate) retro_atomic_fetch_add_int(&gate->pending, 1);
}
void xe_pause_gate_work_end(xe_pause_gate *gate)
{
   if (gate && retro_atomic_fetch_sub_int(&gate->pending, 1) == 1 &&
         retro_atomic_load_acquire_int(&gate->paused))
      retro_eventcount_notify(&gate->changed);
}
int xe_pause_gate_wait_workers(xe_pause_gate *gate, unsigned mask,
      int64_t timeout_us)
{
   int key;
   int64_t start, elapsed, remaining;
   if (!gate || (mask & ~63u) || timeout_us < 0) return 0;
   start = cpu_features_get_time_usec();
   for (;;) {
      if (retro_atomic_load_acquire_int(&gate->stopped) ||
            !retro_atomic_load_acquire_int(&gate->paused)) return 0;
      if (workers_acknowledged(gate, mask)) return 1;
      elapsed = cpu_features_get_time_usec() - start;
      if (elapsed < 0 || elapsed >= timeout_us) return 0;
      remaining = timeout_us - elapsed;
      key = retro_eventcount_prepare_wait(&gate->changed);
      if (retro_atomic_load_acquire_int(&gate->stopped) ||
            !retro_atomic_load_acquire_int(&gate->paused) ||
            workers_acknowledged(gate, mask))
         retro_eventcount_cancel_wait(&gate->changed);
      else retro_eventcount_commit_wait_timeout(&gate->changed, key, remaining);
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
