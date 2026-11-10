#include "core_draw_stats.h"
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

/* Deterministic parked-worker injection is compiled out of normal cores. */
#ifndef XE_JOB_PARK_HOOK
#define XE_JOB_PARK_HOOK(index,phase) ((void)0)
#endif

typedef struct xe_job_worker {
   struct xe_job_pool *pool;
   size_t index;
   sthread_t *thread;
   retro_atomic_int_t ready, parked;
   retro_eventcount_t work;
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
   retro_atomic_int_t stop_from, parked_count, idle_requested;
   size_t wake_cursor; /* Sole producer/control thread, like head/count. */
   /* Every finish writes space. Busy submission reads parked_count; keep
    * those on separate cache lines for both 64- and 128-byte hosts. */
   unsigned char notification_pad[128];
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

/* Keep the busy-pool submit path as small as the former conditional signal. */
static
#if defined(_MSC_VER)
__declspec(noinline)
#elif defined(__GNUC__)
__attribute__((noinline))
#endif
void xe_job_wake_one(xe_job_pool *pool)
{
   size_t scanned, index;
   xe_job_worker *worker;
   index = pool->wake_cursor < pool->count ? pool->wake_cursor : 0;
   for (scanned = 0; scanned < pool->count; ++scanned) {
      worker = pool->workers[index];
      index = index + 1 == pool->count ? 0 : index + 1;
      if (!retro_atomic_load_acquire_int(&worker->parked) ||
            !retro_atomic_cas_int(&worker->parked, 1, 0)) continue;
      retro_atomic_fetch_add_int(&pool->parked_count, -1);
      pool->wake_cursor = index;
      XE_JOB_PARK_HOOK(worker->index, 3);
      /* One job wakes one registered worker, not the whole compiler pool. */
      retro_eventcount_notify(&worker->work);
      return;
   }
}
void xe_job_pool_notify(xe_job_pool *pool)
{
   if (!pool) return;
   /* Pair publication with the registered-waiter recheck. Configuration and
    * notify share the existing sole producer/control owner of the array. */
   retro_atomic_thread_fence_seq_cst();
   if (retro_atomic_load_acquire_int(&pool->parked_count)) xe_job_wake_one(pool);
}

int xe_job_pool_run_one(xe_job_pool *pool, void *context)
{
   uint64_t position, sequence;
   size_t slot;
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
#if XE_GPU_DRAW_STATS
   if(xe_draw_stats_role()!=XE_DRAW_COMPILER)
      XE_DRAW_EVENT(XE_DRAW_GUEST_PIPELINE, XE_DRAW_INLINE_JOB);
#endif
   pool->run(pool->opaque, pool->payloads + slot * pool->stride, context, position);
   /* The callback owns the lease; it may have released/reused the slot already. */
   return 1;
}
int xe_job_pool_finish(xe_job_pool *pool, uint64_t position)
{
   size_t slot;
   int before;
   if (!pool) return 0;
   slot = (size_t)position & (pool->capacity - 1);
   if ((uint64_t)retro_atomic_load_acquire_64(&pool->sequences[slot]) != position + 1 ||
         (int64_t)((uint64_t)retro_atomic_load_acquire_64(&pool->tail) - position) <= 0)
      return 0;
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
   void *context;
   int key;
#if XE_GPU_DRAW_STATS
   unsigned previous_role=xe_draw_stats_enter(XE_DRAW_COMPILER);
#endif
   context=pool->context_new ? pool->context_new(pool->opaque) : NULL;
   sthread_setname(pool->name);
   retro_atomic_store_release_int(&worker->ready, 1);
   retro_eventcount_notify(&pool->startup);
   for (;;) {
      if (worker->index >= (size_t)retro_atomic_load_acquire_int(&pool->stop_from)) break;
      if (xe_job_pool_run_one(pool, context)) continue;
      /* Account before exposing the registration: a notifier may claim the
       * flag immediately. Its decrement must never precede this increment. */
      retro_atomic_fetch_add_int(&pool->parked_count, 1);
      XE_JOB_PARK_HOOK(worker->index, 4);
      retro_atomic_store_release_int(&worker->parked, 1);
      retro_atomic_thread_fence_seq_cst();
      XE_JOB_PARK_HOOK(worker->index, 1);
      if (worker->index < (size_t)retro_atomic_load_acquire_int(&pool->stop_from) &&
            !xe_job_ready(pool)) {
         /* Avoid preparing/cancelling an event when a job is already visible.
          * A notifier may consume our registration before prepare; recheck
          * parked too, so a stolen job cannot leave an unregistered sleeper. */
         key = retro_eventcount_prepare_wait(&worker->work);
         if (worker->index < (size_t)retro_atomic_load_acquire_int(&pool->stop_from) &&
               retro_atomic_load_acquire_int(&worker->parked) && !xe_job_ready(pool)) {
            XE_JOB_PARK_HOOK(worker->index, 2);
            retro_eventcount_commit_wait(&worker->work, key);
         } else {
            retro_eventcount_cancel_wait(&worker->work);
         }
      }
      /* The notifier may already own this registration's count. */
      if (retro_atomic_exchange_int(&worker->parked, 0))
         retro_atomic_fetch_add_int(&pool->parked_count, -1);
   }
   if (pool->context_free) pool->context_free(pool->opaque, context);
   retro_eventcount_thread_cleanup();
#if XE_GPU_DRAW_STATS
   xe_draw_stats_leave(previous_role);
#endif
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
   if (!pool->sequences || !pool->payloads ||
         !retro_eventcount_init(&pool->space) ||
         !retro_eventcount_init(&pool->idle_event) ||
         !retro_eventcount_init(&pool->startup)) {
      /* No workers or constructed payloads exist on initialization failure. */
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
   retro_atomic_int_init(&pool->parked_count, 0);
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
      for (i = count; i < pool->count; ++i)
         retro_eventcount_notify(&pool->workers[i]->work);
      while (pool->count > count) {
         worker = pool->workers[--pool->count];
         sthread_join(worker->thread);
         retro_eventcount_free(&worker->work);
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
      retro_atomic_int_init(&worker->parked, 0);
      if (!retro_eventcount_init(&worker->work)) { free(worker); return 0; }
      /* Preserve the existing shader compiler worker stack budget. */
      worker->thread = sthread_create_with_stack_size(xe_job_worker_run, worker,
            (size_t)4 * 1024 * 1024);
      if (!worker->thread) {
         retro_eventcount_free(&worker->work); free(worker); return 0;
      }
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
void *xe_job_pool_try_begin(xe_job_pool *pool)
{
   size_t slot;
   if(!pool)return NULL;
   slot=(size_t)pool->head & (pool->capacity-1);
   if((uint64_t)retro_atomic_load_acquire_64(&pool->sequences[slot])!=pool->head)
      return NULL;
   return pool->payloads+slot*pool->stride;
}
void *xe_job_pool_begin(xe_job_pool *pool)
{
   size_t slot;
   int key;
   if (!pool) return NULL;
   slot = (size_t)pool->head & (pool->capacity - 1);
   while ((uint64_t)retro_atomic_load_acquire_64(&pool->sequences[slot]) != pool->head) {
      XE_DRAW_EVENT(XE_DRAW_GUEST_PIPELINE, XE_DRAW_ADMISSION_WAIT);
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
      XE_DRAW_EVENT(XE_DRAW_GUEST_PIPELINE, XE_DRAW_COMPILE_WAIT);
      key = retro_eventcount_prepare_wait(&pool->idle_event);
      if (!xe_job_pool_busy(pool)) retro_eventcount_cancel_wait(&pool->idle_event);
      else retro_eventcount_commit_wait(&pool->idle_event, key);
   }
}
void xe_job_pool_await_pending(xe_job_pool *pool, xe_job_pending pending,
      const void *state)
{
   int key;
   if (!pool || !pending) return;
   while (pending(state)) {
      XE_DRAW_EVENT(XE_DRAW_GUEST_PIPELINE, XE_DRAW_COMPILE_WAIT);
      key = retro_eventcount_prepare_wait(&pool->space);
      if (!pending(state)) retro_eventcount_cancel_wait(&pool->space);
      else retro_eventcount_commit_wait(&pool->space, key);
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
   pool->wake_cursor = 0;
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
   retro_eventcount_free(&pool->space); retro_eventcount_free(&pool->idle_event);
   retro_eventcount_free(&pool->startup);
   free((void *)pool->sequences); free(pool->payloads); free(pool);
}
