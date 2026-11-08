#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include "libretro/core_job_pool.h"
#include "libretro/core_pipeline_publication.h"
#include <rthreads/rthreads.h>
#include <retro_atomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %u\n",(unsigned)__LINE__); exit(1); } } while (0)
#define JOBS 14000
struct job { unsigned id, check; unsigned char bytes[47]; };
struct state {
   xe_job_pool *pool;
   retro_atomic_int_t seen[JOBS], processed, created, freed, discarded, helped, idle;
};
struct context { struct state *state; unsigned magic; };
static void *new_context(void *opaque)
{
   struct state *state = (struct state *)opaque;
   struct context *context = (struct context *)malloc(sizeof(*context));
   CHECK(context);
   context->state = state; context->magic = 0x1234;
   retro_atomic_fetch_add_int(&state->created, 1);
   return context;
}
static void free_context(void *opaque, void *value)
{
   struct state *state = (struct state *)opaque;
   struct context *context = (struct context *)value;
   CHECK(context && context->state == state && context->magic == 0x1234);
   context->magic = 0;
   free(context);
   retro_atomic_fetch_add_int(&state->freed, 1);
}
static void run(void *opaque, void *payload, void *value, uint64_t lease)
{
   struct state *state = (struct state *)opaque;
   struct job *job = (struct job *)payload;
   struct context *context = (struct context *)value;
   unsigned i;
   if (context) CHECK(context->state == state && context->magic == 0x1234);
   else retro_atomic_fetch_add_int(&state->helped, 1);
   CHECK(job->id < JOBS && job->check == job->id * 37);
   for (i = 0; i < sizeof(job->bytes); ++i)
      CHECK(job->bytes[i] == (unsigned char)(job->id + i));
   CHECK(retro_atomic_fetch_add_int(&state->seen[job->id], 1) == 0);
   memset(job, 0xcc, sizeof(*job));
   retro_atomic_fetch_add_int(&state->processed, 1);
   CHECK(xe_job_pool_finish(state->pool, lease));
   CHECK(!xe_job_pool_finish(state->pool, lease));
}
static void discard(void *opaque, void *payload)
{
   struct state *state = (struct state *)opaque;
   struct job *job = (struct job *)payload;
   CHECK(job->id < JOBS);
   retro_atomic_fetch_add_int(&state->discarded, 1);
}
static void idle(void *opaque)
{
   struct state *state = (struct state *)opaque;
   retro_atomic_fetch_add_int(&state->idle, 1);
}
static void submit(xe_job_pool *pool, unsigned id)
{
   struct job *job = (struct job *)xe_job_pool_begin(pool);
   unsigned i;
   CHECK(job);
   job->id = id; job->check = id * 37;
   for (i = 0; i < sizeof(job->bytes); ++i)
      job->bytes[i] = (unsigned char)(id + i);
   xe_job_pool_commit(pool);
}
struct held_state { xe_job_pool *pool; struct job *views[8]; uint64_t leases[8]; unsigned count; };
static void hold(void *opaque, void *payload, void *context, uint64_t lease)
{
   struct held_state *state = (struct held_state *)opaque;
   (void)context;
   CHECK(state->count < 8);
   state->views[state->count] = (struct job *)payload;
   state->leases[state->count++] = lease;
}
static void discard_held(void *opaque, void *payload) { (void)opaque; (void)payload; }
static void held_test(void)
{
   struct held_state state;
   unsigned i;
   memset(&state, 0, sizeof(state));
   state.pool = xe_job_pool_new(4, sizeof(struct job), "Held views", hold,
         discard_held, NULL, NULL, NULL, &state);
   CHECK(state.pool);
   for (i = 0; i < 4; ++i) submit(state.pool, i);
   for (i = 0; i < 4; ++i) CHECK(xe_job_pool_run_one(state.pool, NULL));
   CHECK(!xe_job_pool_run_one(state.pool, NULL) && xe_job_pool_busy(state.pool));
   for (i = 0; i < 4; ++i) CHECK(state.views[i]->id == i);
   CHECK(xe_job_pool_finish(state.pool, state.leases[0]));
   submit(state.pool, 4);
   CHECK(!xe_job_pool_finish(state.pool, state.leases[0]));
   for (i = 1; i < 4; ++i) CHECK(state.views[i]->id == i);
   CHECK(xe_job_pool_run_one(state.pool, NULL));
   for (i = 4; i > 0; --i) CHECK(xe_job_pool_finish(state.pool, state.leases[i]));
   CHECK(!xe_job_pool_busy(state.pool));
   xe_job_pool_free(state.pool);
}
struct dependency_state {
   xe_job_pool *pool;
   xe_pipeline_publication publication;
   slock_t *lock;
   scond_t *changed;
   int started, done;
};
static void dependency_waiter(void *opaque)
{
   struct dependency_state *state = (struct dependency_state *)opaque;
   slock_lock(state->lock);
   state->started = 1; scond_signal(state->changed);
   slock_unlock(state->lock);
#ifdef XE_JOB_TEST_GLOBAL_WAIT
   xe_job_pool_await(state->pool);
#else
   xe_job_pool_await_pending(state->pool, xe_pipeline_is_pending, &state->publication);
#endif
   slock_lock(state->lock);
   state->done = 1; scond_signal(state->changed);
   slock_unlock(state->lock);
}
static int dependency_test(void)
{
   struct held_state held;
   struct dependency_state state;
   sthread_t *waiter;
   unsigned round, i;
   int early;
   memset(&held, 0, sizeof(held)); memset(&state, 0, sizeof(state));
   held.pool = xe_job_pool_new(4, sizeof(struct job), "Dependency", hold,
         discard_held, NULL, NULL, NULL, &held);
   CHECK(held.pool);
   state.pool = held.pool; state.lock = slock_new(); state.changed = scond_new();
   CHECK(state.lock && state.changed);
   for (round = 0; round < 128; ++round) {
      held.count = 0; state.started = state.done = 0;
      xe_pipeline_publication_init(&state.publication);
      xe_pipeline_flag_store(&state.publication, XE_PIPELINE_PENDING, 1);
      for (i = 0; i < 4; ++i) { submit(held.pool, i); CHECK(xe_job_pool_run_one(held.pool, NULL)); }
      waiter = sthread_create(dependency_waiter, &state); CHECK(waiter);
      slock_lock(state.lock);
      while (!state.started) CHECK(scond_wait_timeout(state.changed, state.lock, 5000000));
      slock_unlock(state.lock);
      if (round & 1) xe_pipeline_publish_failure(&state.publication);
      else {
         xe_pipeline_handle_store(&state.publication, round + 1);
         xe_pipeline_flag_store(&state.publication, XE_PIPELINE_PENDING, 0);
      }
      CHECK(xe_job_pool_finish(held.pool, held.leases[0]));
      slock_lock(state.lock);
      while (!state.done)
         if (!scond_wait_timeout(state.changed, state.lock, 5000000)) break;
      early = state.done;
      slock_unlock(state.lock);
      CHECK(xe_job_pool_busy(held.pool));
      for (i = 1; i < 4; ++i) CHECK(xe_job_pool_finish(held.pool, held.leases[i]));
      sthread_join(waiter);
      if (!early) {
         fputs("dependency wait drained unrelated held jobs\n", stderr);
         scond_free(state.changed); slock_free(state.lock); xe_job_pool_free(held.pool);
         return 0;
      }
      CHECK(!xe_pipeline_is_pending(&state.publication));
      if (round & 1) CHECK(xe_pipeline_flag_load(&state.publication, XE_PIPELINE_FAILED));
      else CHECK(xe_pipeline_handle_load(&state.publication) == round + 1);
      xe_job_pool_clear(held.pool);
   }
   scond_free(state.changed); slock_free(state.lock); xe_job_pool_free(held.pool);
   puts("PASS: 128 target publications return with three unrelated jobs still held, including failed creation");
   return 1;
}
int main(void)
{
   struct state *state = (struct state *)calloc(1, sizeof(*state));
   xe_job_pool *pool;
   unsigned round, i;
   CHECK(state);
   CHECK(!xe_job_pool_new(1, 8, "test", run, discard, NULL, NULL, NULL, NULL));
   CHECK(!xe_job_pool_new(3, 8, "test", run, discard, NULL, NULL, NULL, NULL));
   CHECK(!xe_job_pool_new(2, (size_t)-1, "test", run, discard, NULL, NULL, NULL, NULL));
   CHECK(!xe_job_pool_set_threads(NULL, 0) && !xe_job_pool_begin(NULL));
   CHECK(!xe_job_pool_run_one(NULL, NULL) && !xe_job_pool_busy(NULL));
   xe_job_pool_notify(NULL); xe_job_pool_await(NULL);
   xe_job_pool_clear(NULL); xe_job_pool_free(NULL);
   for (round = 0; round < 7; ++round) {
      memset(state, 0, sizeof(*state));
      pool = xe_job_pool_new((size_t)2 << round, sizeof(struct job), "Test jobs",
            run, discard, new_context, free_context, idle, state);
      CHECK(pool && xe_job_pool_threads(pool) == 0);
      state->pool = pool;
      CHECK(xe_job_pool_set_threads(pool, 2));
      CHECK(retro_atomic_load_acquire_int(&state->created) == 2);
      xe_job_pool_arm_idle(pool, 1);
      for (i = 0; i < 12000; ++i) {
         submit(pool, i);
         if (i == 103) CHECK(xe_job_pool_set_threads(pool, 6));
         if (i % 113 == 0) xe_job_pool_run_one(pool, NULL);
      }
      xe_job_pool_await(pool);
      CHECK(!xe_job_pool_busy(pool));
      CHECK(xe_job_pool_set_threads(pool, 2));
      for (i = 12000; i < JOBS; ++i) submit(pool, i);
      while (xe_job_pool_run_one(pool, NULL)) {}
      CHECK(xe_job_pool_set_threads(pool, 0));
      xe_job_pool_await(pool);
      CHECK(retro_atomic_load_acquire_int(&state->processed) == JOBS);
      CHECK(retro_atomic_load_acquire_int(&state->idle) == 1);
      CHECK(retro_atomic_load_acquire_int(&state->created) == 6 &&
            retro_atomic_load_acquire_int(&state->freed) == 6);
      for (i = 0; i < JOBS; ++i) CHECK(retro_atomic_load_acquire_int(&state->seen[i]) == 1);
      xe_job_pool_clear(pool);
      CHECK(!xe_job_pool_busy(pool));
      /* Stop with queued objects, clear them, then restart the same storage. */
      submit(pool, 0); submit(pool, 1);
      xe_job_pool_clear(pool);
      CHECK(retro_atomic_load_acquire_int(&state->discarded) == 2);
      CHECK(xe_job_pool_set_threads(pool, 3));
      CHECK(retro_atomic_load_acquire_int(&state->created) == 9);
      xe_job_pool_await(pool);
      xe_job_pool_free(pool);
      CHECK(retro_atomic_load_acquire_int(&state->created) == 9 &&
            retro_atomic_load_acquire_int(&state->freed) == 9);
   }
   held_test();
   CHECK(dependency_test());
   free(state);
   puts("PASS: bounded SPMC jobs, exact-once ownership, context lifetime, growth/shrink, caller drain, idle waits and cancellation");
   return 0;
}
