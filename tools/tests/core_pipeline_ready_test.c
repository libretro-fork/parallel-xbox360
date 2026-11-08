/* An earlier blocked compilation must not hold a later completed draw hostage.
 * Locks, timers and the artificial block are test-only fault injection. */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif
#include "core_pipeline_ready_test.h"
#include "libretro/core_pipeline_publication.h"
#include <rthreads/rthreads.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <time.h>
#endif
struct ready_state {
   void *queue;
   slock_t *lock;
   scond_t *changed;
   unsigned blocked, release, ready, stores[4096], fail, closing, closed;
   xe_pipeline_publication target;
};
static uint64_t now_us(void)
{
#ifdef _WIN32
   LARGE_INTEGER counter, frequency;
   QueryPerformanceCounter(&counter);
   QueryPerformanceFrequency(&frequency);
   return (uint64_t)((double)counter.QuadPart * 1000000.0 /
         (double)frequency.QuadPart);
#else
   struct timespec value;
   clock_gettime(CLOCK_MONOTONIC, &value);
   return (uint64_t)value.tv_sec * UINT64_C(1000000) + value.tv_nsec / 1000;
#endif
}
/* Caller holds lock; spurious wakeups cannot extend the deadline. */
static int wait_value(struct ready_state *state, unsigned *value,
      uint64_t timeout)
{
   uint64_t end, now;
   end = now_us() + timeout;
   while (!*value) {
      now = now_us();
      if (now >= end) return 0;
      scond_wait_timeout(state->changed, state->lock, (int64_t)(end - now));
   }
   return 1;
}
static uintptr_t create(void *opaque, size_t id)
{
   struct ready_state *state;
   state = (struct ready_state *)opaque;
   if (!id) {
      slock_lock(state->lock);
      state->blocked = 1;
      scond_broadcast(state->changed);
      while (!state->release) scond_wait(state->changed, state->lock);
      slock_unlock(state->lock);
   }
   return id == 1 && state->fail ? 0 : id + 1;
}
static void store(void *opaque, size_t id, uintptr_t handle)
{
   struct ready_state *state;
   state = (struct ready_state *)opaque;
   slock_lock(state->lock);
   if (id >= 4096 || state->stores[id]++ ||
         handle != (id == 1 && state->fail ? 0 : id + 1)) {
      fputs("FAIL ready publication identity/duplicate\n", stderr);
      state->ready = 2;
   } else if (id == 1) {
      if (handle) {
         xe_pipeline_handle_store(&state->target, handle);
         xe_pipeline_flag_store(&state->target, XE_PIPELINE_PENDING, 0);
      } else if (state->fail == 1) xe_pipeline_publish_failure(&state->target);
      else xe_pipeline_flag_store(&state->target, XE_PIPELINE_PENDING, 0);
      state->ready = 1;
   }
   scond_broadcast(state->changed);
   slock_unlock(state->lock);
}
static void close_queue(void *opaque)
{
   struct ready_state *state;
   state = (struct ready_state *)opaque;
   slock_lock(state->lock);
   state->closing = 1;
   scond_broadcast(state->changed);
   slock_unlock(state->lock);
   xe_ready_shutdown(state->queue);
   slock_lock(state->lock);
   state->closed = 1;
   scond_broadcast(state->changed);
   slock_unlock(state->lock);
}
static int run_case(unsigned fail, unsigned shutdown_case)
{
   struct ready_state state;
   xe_ready_callbacks callbacks;
   sthread_t *closer;
   unsigned i;
   int passed;
   memset(&state, 0, sizeof(state));
   state.fail = fail;
   state.lock = slock_new();
   state.changed = scond_new();
   if (!state.lock || !state.changed) return 0;
   xe_pipeline_publication_init(&state.target);
   xe_pipeline_flag_store(&state.target, XE_PIPELINE_PENDING, 1);
   callbacks.owner = &state;
   callbacks.create = create;
   callbacks.store = store;
   state.queue = xe_ready_new(&callbacks);
   xe_ready_threads(state.queue, 2);
   xe_ready_push(state.queue, 0);
   xe_ready_push(state.queue, 1);
   slock_lock(state.lock);
   passed = wait_value(&state, &state.blocked, UINT64_C(5000000)) &&
         wait_value(&state, &state.ready, UINT64_C(5000000));
   passed = passed && state.ready == 1 && !state.stores[0];
   slock_unlock(state.lock);
   closer = NULL;
   if (passed) {
      xe_ready_await_target(state.queue, &state.target);
      passed = xe_ready_busy(state.queue) &&
            xe_pipeline_handle_load(&state.target) == (fail ? 0 : 2) &&
            xe_pipeline_flag_load(&state.target, XE_PIPELINE_FAILED) == (int)(fail == 1);
      if (shutdown_case) {
         closer = sthread_create(close_queue, &state);
         if (!closer) passed = 0;
         else {
            slock_lock(state.lock);
            passed = passed && wait_value(&state, &state.closing, UINT64_C(5000000));
            /* A bounded event wait proves shutdown has not returned while its
             * creator still borrows the entry. No sleep/poll in production. */
            if (wait_value(&state, &state.closed, UINT64_C(20000))) passed = 0;
            slock_unlock(state.lock);
         }
      } else {
         /* Fill the remaining capacity while the first slot is still leased.
          * Ring wrap beyond this capacity must still apply backpressure. */
         for (i = 2; i < 4096; ++i) xe_ready_push(state.queue, i);
      }
   }
   /* Always release the fault before reporting failure: the ordered parent
    * must fail the oracle rather than hang during destruction. */
   slock_lock(state.lock);
   state.release = 1;
   scond_broadcast(state.changed);
   slock_unlock(state.lock);
   if (closer) sthread_join(closer);
   else xe_ready_await(state.queue);
   if (!shutdown_case && passed) {
      for (i = 0; i < 4096; ++i) passed = passed && state.stores[i] == 1;
      memset(state.stores, 0, sizeof(state.stores));
      /* Reuse every slot after the retained first lease is released. */
      for (i = 0; i < 4096; ++i) xe_ready_push(state.queue, i);
      xe_ready_await(state.queue);
   }
   xe_ready_shutdown(state.queue);
   xe_ready_free(state.queue);
   passed = passed && state.stores[0] == 1 && state.stores[1] == 1;
   if (!shutdown_case && passed)
      for (i = 2; i < 4096; ++i) passed = passed && state.stores[i] == 1;
   scond_free(state.changed);
   slock_free(state.lock);
   return passed;
}
struct timeline_reader {
   void *timeline;
   unsigned failed;
};
static void read_timeline(void *opaque)
{
   struct timeline_reader *reader;
   uint64_t last, next;
   unsigned i;
   reader = (struct timeline_reader *)opaque;
   last = 1;
   for (i = 0; i < 200000; ++i) {
      next = xe_ready_timeline_value(reader->timeline);
      if (next < last || next > 200001) reader->failed = 1;
      last = next;
   }
}
static int timeline_test(void)
{
   struct timeline_reader readers[4];
   sthread_t *threads[4];
   void *timeline;
   unsigned i, made;
   int passed;
   timeline = xe_ready_timeline_new();
   passed = xe_ready_timeline_value(timeline) == 1;
   made = 0;
   for (i = 0; i < 4; ++i) {
      readers[i].timeline = timeline;
      readers[i].failed = 0;
      threads[i] = sthread_create(read_timeline, &readers[i]);
      if (!threads[i]) { passed = 0; break; }
      ++made;
   }
   for (i = 0; i < 200000; ++i) xe_ready_timeline_advance(timeline);
   for (i = 0; i < made; ++i) {
      sthread_join(threads[i]);
      passed = passed && !readers[i].failed;
   }
   passed = passed && xe_ready_timeline_value(timeline) == 200001;
   xe_ready_timeline_free(timeline);
   return passed;
}
int xe_ready_test(void)
{
   unsigned fail, closing;
   if (!timeline_test()) {
      fputs("FAIL submission timeline concurrent readers\n", stderr);
      return 0;
   }
   for (fail = 0; fail != 3; ++fail)
      for (closing = 0; closing != 2; ++closing)
         if (!run_case(fail, closing)) {
            fprintf(stderr, "FAIL ready predecessor fail=%u shutdown=%u\n", fail, closing);
            return 0;
         }
   puts("PASS C89: blocked predecessor, Vulkan/D3D12 failure policies, capacity, joined shutdown and shared submission timeline");
   return 1;
}
