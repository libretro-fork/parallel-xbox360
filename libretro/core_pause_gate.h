#ifndef XE_CORE_PAUSE_GATE_H
#define XE_CORE_PAUSE_GATE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xe_pause_gate xe_pause_gate;
xe_pause_gate *xe_pause_gate_new(void);
void xe_pause_gate_set(xe_pause_gate *gate, int paused);
int xe_pause_gate_is_paused(const xe_pause_gate *gate);
/* Wait while paused. Resume and stop explicitly wake all waiting workers. */
int xe_pause_gate_wait(xe_pause_gate *gate);
/* Indexed workers acknowledge quiescence only after leaving their work.
 * There are six guest CPU participants. No allocation or mutex is added to
 * ordinary dispatch. A controller owns pause/resume transitions. */
int xe_pause_gate_wait_index(xe_pause_gate *gate, unsigned index);
/* Queued owner callbacks must not acknowledge a later pause after timing out. */
int xe_pause_gate_generation(const xe_pause_gate *gate);
int xe_pause_gate_wait_generation(xe_pause_gate *gate, unsigned index,
      int generation);
int xe_pause_gate_wait_workers(xe_pause_gate *gate, unsigned mask,
      int64_t timeout_us);
/* Count borrowed host operations before publishing them; retire after their
 * final access. The snapshot controller first stops all their producers. */
void xe_pause_gate_work_begin(xe_pause_gate *gate);
void xe_pause_gate_work_end(xe_pause_gate *gate);
void xe_pause_gate_stop(xe_pause_gate *gate);
/* Caller must stop and join all workers before freeing their gate. */
void xe_pause_gate_free(xe_pause_gate *gate);
#ifdef __cplusplus
}
#endif
#endif
