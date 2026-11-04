#ifndef XE_CORE_JOB_POOL_H
#define XE_CORE_JOB_POOL_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xe_job_pool xe_job_pool;
typedef void (*xe_job_run)(void *opaque, void *payload, void *context);
typedef void (*xe_job_discard)(void *opaque, void *payload);
typedef void *(*xe_job_context_new)(void *opaque);
typedef void (*xe_job_context_free)(void *opaque, void *context);
typedef void (*xe_job_idle)(void *opaque);
/* One producer/control thread; multiple consumers. Payloads are borrowed by
 * run until it returns. run/discard own any C++ payload destruction. All ring
 * storage is allocated here. Thread/context growth is cold configuration. */
xe_job_pool *xe_job_pool_new(size_t capacity, size_t payload_size,
      const char *name, xe_job_run run, xe_job_discard discard,
      xe_job_context_new context_new, xe_job_context_free context_free,
      xe_job_idle idle, void *opaque);
int xe_job_pool_set_threads(xe_job_pool *pool, size_t count);
size_t xe_job_pool_threads(const xe_job_pool *pool);
/* One begin/commit at a time. Full rings wait for a worker to release a slot.
 * No object construction may throw between begin and commit. */
void *xe_job_pool_begin(xe_job_pool *pool);
void xe_job_pool_commit(xe_job_pool *pool);
int xe_job_pool_run_one(xe_job_pool *pool, void *context);
int xe_job_pool_busy(xe_job_pool *pool);
void xe_job_pool_notify(xe_job_pool *pool);
void xe_job_pool_await(xe_job_pool *pool);
/* Arm an idle callback, then recheck busy. If already idle, disarm and handle
 * completion on the caller. Exchanges synchronize with the final worker. */
void xe_job_pool_arm_idle(xe_job_pool *pool, int armed);
/* Stop/join workers before clear; producer must also be quiescent. */
void xe_job_pool_clear(xe_job_pool *pool);
void xe_job_pool_free(xe_job_pool *pool);
#ifdef __cplusplus
}
#endif
#endif
