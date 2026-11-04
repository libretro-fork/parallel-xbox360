#include "libretro/core_pause_gate.h"
#include <retro_atomic.h>
#include <rthreads/rthreads.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %u\n", (unsigned)__LINE__); exit(1); } } while (0)
static xe_pause_gate *gate;
static retro_atomic_int_t counts[6];
static unsigned indices[6] = {0, 1, 2, 3, 4, 5};
static void worker(void *opaque)
{
   unsigned index = *(unsigned *)opaque;
   while (xe_pause_gate_wait_index(gate, index))
      retro_atomic_fetch_add_int(&counts[index], 1);
}
int main(void)
{
   sthread_t *threads[6];
   unsigned i, round, check;
   int frozen[6], stale;
   gate = xe_pause_gate_new(); CHECK(gate);
   CHECK(!xe_pause_gate_wait_workers(gate, 63, 0));
   xe_pause_gate_set(gate, 1);
   stale = xe_pause_gate_generation(gate);
   xe_pause_gate_set(gate, 0);
   xe_pause_gate_set(gate, 1);
   CHECK(xe_pause_gate_wait_generation(gate, 0, stale));
   CHECK(!xe_pause_gate_wait_workers(gate, 1, 0));
   CHECK(!xe_pause_gate_wait_workers(gate, 64, 1000));
   CHECK(!xe_pause_gate_wait_workers(gate, 63, 1000));
   for (i = 0; i < 6; ++i) { threads[i] = sthread_create(worker, &indices[i]); CHECK(threads[i]); }
   CHECK(xe_pause_gate_wait_workers(gate, 63, 2000000));
   xe_pause_gate_work_begin(gate);
   CHECK(!xe_pause_gate_wait_workers(gate, 63, 1000));
   xe_pause_gate_work_end(gate);
   CHECK(xe_pause_gate_wait_workers(gate, 63, 0));
   for (round = 0; round < 2000; ++round) {
      xe_pause_gate_set(gate, 0);
      xe_pause_gate_set(gate, 1);
      CHECK(xe_pause_gate_wait_workers(gate, 63, 2000000));
      for (i = 0; i < 6; ++i) frozen[i] = retro_atomic_load_acquire_int(&counts[i]);
      for (check = 0; check < 100; ++check)
         for (i = 0; i < 6; ++i) CHECK(retro_atomic_load_acquire_int(&counts[i]) == frozen[i]);
   }
   xe_pause_gate_stop(gate);
   for (i = 0; i < 6; ++i) sthread_join(threads[i]);
   CHECK(!xe_pause_gate_wait_workers(gate, 63, 0));
   xe_pause_gate_free(gate);
   puts("PASS: six workers quiesce across 2000 racing pause generations, timeout and stopped teardown");
   return 0;
}
