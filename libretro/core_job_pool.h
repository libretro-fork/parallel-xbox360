#ifndef XE_CORE_JOB_POOL_H
#define XE_CORE_JOB_POOL_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xe_job_pool xe_job_pool;
typedef void (*xe_job_run)(void *opaque, void *payload, void *context, uint64_t lease);
typedef void (*xe_job_discard)(void *opaque, void *payload);
typedef void *(*xe_job_context_new)(void *opaque);
typedef void (*xe_job_context_free)(void *opaque, void *context);
typedef void (*xe_job_idle)(void *opaque);
/* One producer/control thread; multiple consumers. run owns each claimed
 * payload until finish, even after run returns. run/discard own destruction. All ring
 * storage is allocated here. Thread/context growth is cold configuration. */
xe_job_pool *xe_job_pool_new(size_t capacity, size_t payload_size,
      const char *name, xe_job_run run, xe_job_discard discard,
      xe_job_context_new context_new, xe_job_context_free context_free,
      xe_job_idle idle, void *opaque);
int xe_job_pool_set_threads(xe_job_pool *pool, size_t count);
size_t xe_job_pool_threads(const xe_job_pool *pool);
/* One begin/commit at a time. Full rings wait for a worker to release a slot.
 * No object construction may throw between begin and commit. */
/* Nonblocking alternative: NULL changes no cursor and retains caller ownership.
 * Shares begin/commit's sole-producer and one-uncommitted-lease contract. */
void *xe_job_pool_try_begin(xe_job_pool *pool);
void *xe_job_pool_begin(xe_job_pool *pool);
void xe_job_pool_commit(xe_job_pool *pool);
int xe_job_pool_run_one(xe_job_pool *pool, void *context);
/* A claimed payload remains owned until finish, which may run on another
 * worker after deferred processing. Destroy the payload before this call and
 * never access it afterward. Exactly one owner finishes each lease. */
int xe_job_pool_finish(xe_job_pool *pool, uint64_t lease);
int xe_job_pool_busy(xe_job_pool *pool);
void xe_job_pool_notify(xe_job_pool *pool);
void xe_job_pool_await(xe_job_pool *pool);
/* Condition storage and pool outlive the wait. A queued job must clear pending
 * before finishing its lease; later unrelated jobs need not finish. */
typedef int (*xe_job_pending)(const void *);
void xe_job_pool_await_pending(xe_job_pool *, xe_job_pending, const void *);

/* Arm an idle callback, then recheck busy. If already idle, disarm and handle
 * completion on the caller. Exchanges synchronize with the final worker. */
void xe_job_pool_arm_idle(xe_job_pool *pool, int armed);
/* Stop/join workers and finish all claimed leases before clear; producer
 * must also be quiescent. Clear cancels only unclaimed queued payloads. */
void xe_job_pool_clear(xe_job_pool *pool);
void xe_job_pool_free(xe_job_pool *pool);
#ifdef __cplusplus
}
#endif
#endif
