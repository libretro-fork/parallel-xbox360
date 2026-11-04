#ifndef XE_CORE_PAUSE_GATE_H
#define XE_CORE_PAUSE_GATE_H
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xe_pause_gate xe_pause_gate;
xe_pause_gate *xe_pause_gate_new(void);
void xe_pause_gate_set(xe_pause_gate *gate, int paused);
int xe_pause_gate_is_paused(const xe_pause_gate *gate);
/* Wait while paused. Resume and stop explicitly wake all waiting workers. */
int xe_pause_gate_wait(xe_pause_gate *gate);
void xe_pause_gate_stop(xe_pause_gate *gate);
/* Caller must stop and join all workers before freeing their gate. */
void xe_pause_gate_free(xe_pause_gate *gate);
#ifdef __cplusplus
}
#endif
#endif
