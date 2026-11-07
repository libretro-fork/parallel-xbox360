/* Single control owner; stop may be published by another thread. MSVC C89.
 * Immutable stage table and context must outlive every control call. Callbacks
 * must not throw, and release must undo even a partially failed acquire.
 * Resource workers are stopped/joined externally before owner destruction. */
#ifndef XE_CORE_STATE_CONTROL_H
#define XE_CORE_STATE_CONTROL_H
#include <stdint.h>
#include <retro_atomic.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xe_state_stage {
 int (*paused)(void *);
 int (*acquire)(void *,int64_t);
 void (*release)(void *);
} xe_state_stage;
typedef int64_t (*xe_state_control_clock)(void *);
typedef struct xe_state_control {
 const xe_state_stage *stages;
 void *context;
 xe_state_control_clock clock;
 unsigned count;
 uint32_t owned;
 retro_atomic_int_t state,stopped;
} xe_state_control;
/* Cold init only, no concurrent users. NULL clock uses owned common host time
 * solely to bound this operation. No allocation and no stage metadata copy. */
int xe_state_control_init(xe_state_control *,const xe_state_stage *,unsigned,
 void *,xe_state_control_clock);
/* Idempotent while held by the same owner. Reentrant transitions fail rather
 * than block. Every stage verifies quiescence, even if already paused. */
int xe_state_control_acquire(xe_state_control *,int64_t);
/* Reverse release only stages this operation acquired. Idempotent when idle. */
int xe_state_control_release(xe_state_control *);
int xe_state_control_held(const xe_state_control *);
int xe_state_control_busy(const xe_state_control *);
/* Sticky; rejects new acquisitions, makes an in-flight acquisition unwind.
 * Does not run callbacks on the stopping thread or revoke an already held
 * operation. Retain the controller until its owner returns and workers join. */
void xe_state_control_stop(xe_state_control *);
#ifdef __cplusplus
}
#endif
#endif
