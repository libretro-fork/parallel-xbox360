#include "core_bulk_work.h"
#include <rthreads/rthreads.h>
#include <stdlib.h>

typedef struct xe_bulk_call {
   void (*run)(void *);
   void *opaque;
   const char *name;
} xe_bulk_call;

int xe_bulk_work_init(xe_bulk_work *work, size_t count)
{
   if (!work || (uint64_t)count > (uint64_t)INT64_MAX) return 0;
   retro_atomic_64_init(&work->next, 0);
   retro_atomic_64_init(&work->completed, 0);
   retro_atomic_int_init(&work->failed, 0);
   work->count = count;
   return 1;
}
int xe_bulk_work_claim(xe_bulk_work *work, size_t *index)
{
   int64_t next;
   for (;;) {
      if (retro_atomic_load_acquire_int(&work->failed)) return 0;
      next = retro_atomic_load_relaxed_64(&work->next);
      if ((uint64_t)next >= (uint64_t)work->count) return 0;
      if (retro_atomic_cas_64(&work->next, next, next + 1)) {
         *index = (size_t)next;
         return 1;
      }
   }
}
int xe_bulk_work_fail(xe_bulk_work *work)
{
   return retro_atomic_cas_int(&work->failed, 0, 1);
}
void xe_bulk_work_complete(xe_bulk_work *work, size_t count)
{
   int64_t old;
   if (!count) return;
   do {
      old = retro_atomic_load_relaxed_64(&work->completed);
   } while (!retro_atomic_cas_64(&work->completed, old, old + (int64_t)count));
}
size_t xe_bulk_work_completed(const xe_bulk_work *work)
{
   return (size_t)retro_atomic_load_acquire_64(&work->completed);
}
static void xe_bulk_thread(void *opaque)
{
   xe_bulk_call *call = (xe_bulk_call *)opaque;
   if (call->name) sthread_setname(call->name);
   call->run(call->opaque);
}
void xe_bulk_work_run(size_t background_workers, size_t jobs,
      void (*run)(void *), void *opaque, const char *name)
{
   xe_bulk_call call;
   sthread_t **threads = NULL;
   size_t started = 0, i;
   if (!jobs || !run) return;
   /* The caller also consumes jobs. Never create a worker for the only job. */
   if (background_workers >= jobs) background_workers = jobs - 1;
   call.run = run;
   call.opaque = opaque;
   call.name = name;
   if (background_workers && background_workers <= (size_t)-1 / sizeof(*threads))
      threads = (sthread_t **)malloc(background_workers * sizeof(*threads));
   if (threads) {
      for (i = 0; i < background_workers; ++i) {
         sthread_t *thread = sthread_create_with_stack_size(xe_bulk_thread,
               &call, 4 * 1024 * 1024);
         if (!thread) break;
         threads[started++] = thread;
      }
   }
   /* Allocation/creation failures leave the remaining jobs for existing
    * workers and the caller. No task bytes are copied or dropped. */
   run(opaque);
   for (i = 0; i < started; ++i) sthread_join(threads[i]);
   free(threads);
}
