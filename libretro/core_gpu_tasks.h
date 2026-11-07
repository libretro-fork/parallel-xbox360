/* Fixed GPU control messages. Many producers; one consumer. MSVC C89.
 * Callback arguments borrow owned queue storage through finish, never beyond.
 * Callers must retain the queue/owner through close and consumer/producer joins. */
#ifndef XE_CORE_GPU_TASKS_H
#define XE_CORE_GPU_TASKS_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define XE_GPU_TASK_SMALL_COUNT 64
#define XE_GPU_TASK_SMALL_BYTES 64
#define XE_GPU_TASK_LARGE_COUNT 4
#define XE_GPU_TASK_LARGE_BYTES 4160
typedef void (*xe_gpu_task_fn)(void *,const void *,size_t);
typedef void (*xe_gpu_task_wake)(void *);
typedef struct xe_gpu_tasks xe_gpu_tasks;
typedef struct xe_gpu_task {
  xe_gpu_task_fn run, cancel;
  void *context;
  const void *data;
  size_t size;
} xe_gpu_task;
typedef struct xe_gpu_task_view { const xe_gpu_task *task; uint64_t lease; } xe_gpu_task_view;
int xe_gpu_tasks_payload_valid(const void *,size_t,const void *,size_t);
xe_gpu_tasks *xe_gpu_tasks_new(xe_gpu_task_wake wake,void *context);
/* Atomic identity only, never dereferenced by the queue. Set before worker
 * start; clear after join, before releasing the native worker object. */
void xe_gpu_tasks_set_owner(xe_gpu_tasks *,void *);
void *xe_gpu_tasks_owner(xe_gpu_tasks *);
/* No allocation, lock or wait. Failure leaves the payload with the caller.
 * Each successful publication wakes the consumer before releasing its guard. */
int xe_gpu_tasks_submit(xe_gpu_tasks *,xe_gpu_task_fn,xe_gpu_task_fn,void *,
    const void *,size_t,const void *,size_t);
/* Consumer only; empty probes are never used by producers. */
int xe_gpu_tasks_empty(xe_gpu_tasks *);
int xe_gpu_tasks_take(xe_gpu_tasks *,xe_gpu_task_view *);
int xe_gpu_tasks_finish(xe_gpu_tasks *,const xe_gpu_task_view *);
/* Close rejects new submissions; accepted publishers may still be finishing.
 * Consumer drains/cancels until closed_ready, then joins precede free. */
void xe_gpu_tasks_close(xe_gpu_tasks *);
/* Cold, no concurrent producers/consumer; preserve lease generations. */
int xe_gpu_tasks_reopen(xe_gpu_tasks *);
/* After the worker joins. Cancel callbacks must not throw. Waits only for
 * already accepted publishers to finish, never for a GPU/frontend callback. */
void xe_gpu_tasks_cancel_all(xe_gpu_tasks *);
int xe_gpu_tasks_closed(xe_gpu_tasks *);
int xe_gpu_tasks_closed_ready(xe_gpu_tasks *);
/* Refuses to free live views/messages/producers. NULL is accepted. */
int xe_gpu_tasks_free(xe_gpu_tasks *);
/* Copy metadata into caller storage; native callbacks run outside C frames. */
int xe_gpu_tasks_copy_parts(void *,size_t,const void *,size_t,const void *,size_t);
#ifdef __cplusplus
}
#endif
#endif
