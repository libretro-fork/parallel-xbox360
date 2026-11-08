/* C89 fault-injection driver; C++ bridge only forwards typed callbacks. */
#ifndef XE_PIPELINE_READY_TEST_H
#define XE_PIPELINE_READY_TEST_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xe_ready_callbacks {
   void *owner;
   uintptr_t (*create)(void *, size_t);
   void (*store)(void *, size_t, uintptr_t);
} xe_ready_callbacks;
void *xe_ready_new(const xe_ready_callbacks *callbacks);
void xe_ready_free(void *queue);
void xe_ready_push(void *queue, size_t id);
void xe_ready_threads(void *queue, size_t count);
void xe_ready_shutdown(void *queue);
void xe_ready_await(void *queue);
int xe_ready_busy(void *queue);
void xe_ready_await_target(void *queue, const void *target);
void *xe_ready_timeline_new(void);
void xe_ready_timeline_free(void *timeline);
void xe_ready_timeline_advance(void *timeline);
uint64_t xe_ready_timeline_value(void *timeline);
int xe_ready_test(void);
#ifdef __cplusplus
}
#endif
#endif
