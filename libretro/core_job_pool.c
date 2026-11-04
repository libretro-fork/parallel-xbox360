#include "core_job_pool.h"
#include <retro_atomic.h>
#include <rthreads/retro_eventcount.h>
#include <rthreads/rthreads.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#if !defined(RETRO_ATOMIC_HAS_64)
#error The Xbox 360 job pool requires 64-bit atomics
#endif

typedef struct xe_job_worker {
   struct xe_job_pool *pool;
   size_t index;
   sthread_t *thread;
   retro_atomic_int_t ready;
} xe_job_worker;
struct xe_job_pool {
   retro_atomic_64_t *sequences;
   unsigned char *payloads;
   size_t capacity, stride;
   xe_job_run run;
   xe_job_discard discard;
   xe_job_context_new context_new;
   xe_job_context_free context_free;
   xe_job_idle idle;
   void *opaque;
   xe_job_worker **workers;
   size_t count, worker_capacity;
   char name[64];
   /* Keep immutable callback/configuration loads off the cursor cache lines.
    * Separate producer, consumer and completion writes on both 64-byte and
    * 128-byte cache-line hosts without requiring an over-aligned allocation. */
   unsigned char config_pad[128];
   uint64_t head;
   unsigned char producer_pad[128];
   retro_atomic_64_t tail;
   unsigned char consumer_pad[128];
   retro_atomic_int_t outstanding;
   unsigned char completion_pad[128];
   retro_atomic_int_t stop_from, waiters, idle_requested;
   slock_t *park_lock;
   scond_t *park_cond;
   retro_eventcount_t space, idle_event, startup;
};
static int xe_job_ready(xe_job_pool *pool)
{
   uint64_t tail, sequence;
   for (;;) {
      tail = (uint64_t)retro_atomic_load_acquire_64(&pool->tail);
      sequence = (uint64_t)retro_atomic_load_acquire_64(
            &pool->sequences[(size_t)tail & (pool->capacity - 1)]);
      if (sequence == tail + 1) return 1;
      /* A newer sequence means another consumer moved the tail while this
       * one inspected the slot, not an empty queue. Retry its current tail. */
      if ((int64_t)(sequence - tail - 1) < 0) return 0;
   }
}

void xe_job_pool_notify(xe_job_pool *pool)
{
   if (!pool) return;
   /* Pair the publication with the idle worker's registered-waiter recheck.
    * Both sides need store/load ordering: acquire/release alone can miss it. */
   retro_atomic_thread_fence_seq_cst();
   if (retro_atomic_load_acquire_int(&pool->waiters)) {
      slock_lock(pool->park_lock);
      scond_signal(pool->park_cond);
      slock_unlock(pool->park_lock);
   }
}
int xe_job_pool_run_one(xe_job_pool *pool, void *context)
{
   uint64_t position, sequence;
   size_t slot;
   int before;
   if (!pool) return 0;
   for (;;) {
      position = (uint64_t)retro_atomic_load_acquire_64(&pool->tail);
      slot = (size_t)position & (pool->capacity - 1);
      sequence = (uint64_t)retro_atomic_load_acquire_64(&pool->sequences[slot]);
      if (sequence != position + 1) {
         if ((int64_t)(sequence - position - 1) < 0) return 0;
         continue;
      }
      if (retro_atomic_cas_64(&pool->tail, (int64_t)position,
               (int64_t)(position + 1))) break;
   }
   pool->run(pool->opaque, pool->payloads + slot * pool->stride, context);
   retro_atomic_store_release_64(&pool->sequences[slot],
         (int64_t)(position + pool->capacity));
   retro_eventcount_notify(&pool->space);
   before = retro_atomic_fetch_add_int(&pool->outstanding, -1);
   if (before == 1) {
      retro_eventcount_notify(&pool->idle_event);
      if (retro_atomic_exchange_int(&pool->idle_requested, 0) && pool->idle)
         pool->idle(pool->opaque);
   }
   return 1;
}
static void xe_job_worker_run(void *opaque)
{
   xe_job_worker *worker = (xe_job_worker *)opaque;
   xe_job_pool *pool = worker->pool;
   void *context = pool->context_new ? pool->context_new(pool->opaque) : NULL;
   sthread_setname(pool->name);
   retro_atomic_store_release_int(&worker->ready, 1);
   retro_eventcount_notify(&pool->startup);
   for (;;) {
      if (worker->index >= (size_t)retro_atomic_load_acquire_int(&pool->stop_from)) break;
      if (xe_job_pool_run_one(pool, context)) continue;
      slock_lock(pool->park_lock);
      retro_atomic_fetch_add_int(&pool->waiters, 1);
      retro_atomic_thread_fence_seq_cst();
      if (worker->index < (size_t)retro_atomic_load_acquire_int(&pool->stop_from) &&
            !xe_job_ready(pool))
         scond_wait(pool->park_cond, pool->park_lock);
      retro_atomic_fetch_add_int(&pool->waiters, -1);
      slock_unlock(pool->park_lock);
   }
   if (pool->context_free) pool->context_free(pool->opaque, context);
}
xe_job_pool *xe_job_pool_new(size_t capacity, size_t payload_size,
      const char *name, xe_job_run run, xe_job_discard discard,
      xe_job_context_new context_new, xe_job_context_free context_free,
      xe_job_idle idle, void *opaque)
{
   xe_job_pool *pool;
   size_t stride, i, name_size;
   if (capacity < 2 || capacity > INT_MAX / 4 || (capacity & (capacity - 1)) ||
         !payload_size || payload_size > (size_t)-1 - 15 || !run || !discard)
      return NULL;
   stride = (payload_size + 15) & ~(size_t)15;
   if (capacity > (size_t)-1 / stride ||
         capacity > (size_t)-1 / sizeof(retro_atomic_64_t)) return NULL;
   pool = (xe_job_pool *)calloc(1, sizeof(*pool));
   if (!pool) return NULL;
   pool->sequences = (retro_atomic_64_t *)malloc(capacity * sizeof(*pool->sequences));
   pool->payloads = (unsigned char *)malloc(capacity * stride);
   pool->park_lock = slock_new();
   pool->park_cond = scond_new();
   if (!pool->sequences || !pool->payloads || !pool->park_lock || !pool->park_cond ||
         !retro_eventcount_init(&pool->space) ||
         !retro_eventcount_init(&pool->idle_event) ||
         !retro_eventcount_init(&pool->startup)) {
      /* No workers or constructed payloads exist on initialization failure. */
      if (pool->park_cond) scond_free(pool->park_cond);
      if (pool->park_lock) slock_free(pool->park_lock);
      retro_eventcount_free(&pool->space);
      retro_eventcount_free(&pool->idle_event);
      retro_eventcount_free(&pool->startup);
      free((void *)pool->sequences); free(pool->payloads); free(pool);
      return NULL;
   }
   pool->capacity = capacity; pool->stride = stride;
   pool->run = run; pool->discard = discard;
   pool->context_new = context_new; pool->context_free = context_free;
   pool->idle = idle; pool->opaque = opaque;
   retro_atomic_64_init(&pool->tail, 0);
   retro_atomic_int_init(&pool->outstanding, 0);
   retro_atomic_int_init(&pool->stop_from, INT_MAX);
   retro_atomic_int_init(&pool->waiters, 0);
   retro_atomic_int_init(&pool->idle_requested, 0);
   for (i = 0; i < capacity; ++i) retro_atomic_64_init(&pool->sequences[i], (int64_t)i);
   name_size = name ? strlen(name) : 0;
   if (name_size >= sizeof(pool->name)) name_size = sizeof(pool->name) - 1;
   if (name_size) memcpy(pool->name, name, name_size);
   pool->name[name_size] = 0;
   return pool;
}
int xe_job_pool_set_threads(xe_job_pool *pool, size_t count)
{
   xe_job_worker **workers;
   xe_job_worker *worker;
   size_t old_count, i;
   int key;
   if (!pool || count > INT_MAX / 2 || count > (size_t)-1 / sizeof(*workers)) return 0;
   old_count = pool->count;
   if (count < pool->count) {
      retro_atomic_store_release_int(&pool->stop_from, (int)count);
      slock_lock(pool->park_lock);
      scond_broadcast(pool->park_cond);
      slock_unlock(pool->park_lock);
      while (pool->count > count) {
         worker = pool->workers[--pool->count];
         sthread_join(worker->thread);
         free(worker);
      }
      retro_atomic_store_release_int(&pool->stop_from, INT_MAX);
   }
   if (count > pool->worker_capacity) {
      workers = (xe_job_worker **)realloc(pool->workers, count * sizeof(*workers));
      if (!workers) return 0;
      pool->workers = workers; pool->worker_capacity = count;
   }
   while (pool->count < count) {
      worker = (xe_job_worker *)calloc(1, sizeof(*worker));
      if (!worker) return 0;
      worker->pool = pool; worker->index = pool->count;
      retro_atomic_int_init(&worker->ready, 0);
      /* Preserve the existing shader compiler worker stack budget. */
      worker->thread = sthread_create_with_stack_size(xe_job_worker_run, worker,
            (size_t)4 * 1024 * 1024);
      if (!worker->thread) { free(worker); return 0; }
      pool->workers[pool->count++] = worker;
   }
   for (i = old_count; i < pool->count; ++i) {
      worker = pool->workers[i];
      while (!retro_atomic_load_acquire_int(&worker->ready)) {
         key = retro_eventcount_prepare_wait(&pool->startup);
         if (retro_atomic_load_acquire_int(&worker->ready))
            retro_eventcount_cancel_wait(&pool->startup);
         else retro_eventcount_commit_wait(&pool->startup, key);
      }
   }
   return 1;
}
size_t xe_job_pool_threads(const xe_job_pool *pool) { return pool ? pool->count : 0; }
void *xe_job_pool_begin(xe_job_pool *pool)
{
   size_t slot;
   int key;
   if (!pool) return NULL;
   slot = (size_t)pool->head & (pool->capacity - 1);
   while ((uint64_t)retro_atomic_load_acquire_64(&pool->sequences[slot]) != pool->head) {
      key = retro_eventcount_prepare_wait(&pool->space);
      if ((uint64_t)retro_atomic_load_acquire_64(&pool->sequences[slot]) == pool->head)
         retro_eventcount_cancel_wait(&pool->space);
      else retro_eventcount_commit_wait(&pool->space, key);
   }
   return pool->payloads + slot * pool->stride;
}
void xe_job_pool_commit(xe_job_pool *pool)
{
   size_t slot;
   if (!pool) return;
   slot = (size_t)pool->head & (pool->capacity - 1);
   retro_atomic_fetch_add_int(&pool->outstanding, 1);
   retro_atomic_store_release_64(&pool->sequences[slot], (int64_t)(pool->head + 1));
   ++pool->head;
   xe_job_pool_notify(pool);
}
int xe_job_pool_busy(xe_job_pool *pool)
{
   return pool && retro_atomic_load_acquire_int(&pool->outstanding) != 0;
}
void xe_job_pool_arm_idle(xe_job_pool *pool, int armed)
{
   if (pool) retro_atomic_exchange_int(&pool->idle_requested, armed != 0);
}
void xe_job_pool_await(xe_job_pool *pool)
{
   int key;
   if (!pool) return;
   while (xe_job_pool_busy(pool)) {
      key = retro_eventcount_prepare_wait(&pool->idle_event);
      if (!xe_job_pool_busy(pool)) retro_eventcount_cancel_wait(&pool->idle_event);
      else retro_eventcount_commit_wait(&pool->idle_event, key);
   }
}
void xe_job_pool_clear(xe_job_pool *pool)
{
   uint64_t position;
   size_t slot, i;
   if (!pool) return;
   position = (uint64_t)retro_atomic_load_relaxed_64(&pool->tail);
   while (position != pool->head) {
      slot = (size_t)position & (pool->capacity - 1);
      pool->discard(pool->opaque, pool->payloads + slot * pool->stride);
      ++position;
   }
   pool->head = 0;
   retro_atomic_store_relaxed_64(&pool->tail, 0);
   for (i = 0; i < pool->capacity; ++i)
      retro_atomic_store_relaxed_64(&pool->sequences[i], (int64_t)i);
   retro_atomic_store_relaxed_int(&pool->outstanding, 0);
   retro_atomic_store_relaxed_int(&pool->idle_requested, 0);
}
void xe_job_pool_free(xe_job_pool *pool)
{
   if (!pool) return;
   xe_job_pool_set_threads(pool, 0);
   xe_job_pool_clear(pool);
   free(pool->workers);
   scond_free(pool->park_cond); slock_free(pool->park_lock);
   retro_eventcount_free(&pool->space); retro_eventcount_free(&pool->idle_event);
   retro_eventcount_free(&pool->startup);
   free((void *)pool->sequences); free(pool->payloads); free(pool);
}
