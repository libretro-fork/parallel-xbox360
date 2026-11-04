#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include "libretro/core_job_pool.h"
#include <retro_atomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %u\n",(unsigned)__LINE__); exit(1); } } while (0)
#define JOBS 14000
struct job { unsigned id, check; unsigned char bytes[47]; };
struct state {
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
static void run(void *opaque, void *payload, void *value)
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
   free(state);
   puts("PASS: bounded SPMC jobs, exact-once ownership, context lifetime, growth/shrink, caller drain, idle waits and cancellation");
   return 0;
}
