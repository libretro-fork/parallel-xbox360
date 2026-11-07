#include "core_state_control.h"
#include "core_state_budget.h"
#include <features/features_cpu.h>
#include <stddef.h>
#if !defined(RETRO_ATOMIC_LOCK_FREE)
#error State control requires real atomic transitions
#endif
enum { CONTROL_IDLE,CONTROL_ACQUIRE,CONTROL_HELD,CONTROL_RELEASE };
static int64_t host_time(void *context) {
 (void)context;return cpu_features_get_time_usec();
}
int xe_state_control_init(xe_state_control *c,const xe_state_stage *stages,
 unsigned count,void *context,xe_state_control_clock clock) {
 unsigned i;
 if(!c || !stages || !count || count>32) return 0;
 for(i=0;i<count;++i) if(!stages[i].paused || !stages[i].acquire || !stages[i].release) return 0;
 c->stages=stages;c->count=count;c->context=context;c->clock=clock?clock:host_time;
 c->owned=0;retro_atomic_int_init(&c->state,CONTROL_IDLE);retro_atomic_int_init(&c->stopped,0);
 return 1;
}
static void unwind(xe_state_control *c) {
 unsigned i=c->count;
 while(i) {
  uint32_t bit=UINT32_C(1)<<--i;
  if(c->owned&bit) {c->owned&=~bit;c->stages[i].release(c->context);}
 }
 retro_atomic_store_release_int(&c->state,CONTROL_IDLE);
}
int xe_state_control_acquire(xe_state_control *c,int64_t timeout) {
 xe_state_budget budget;
 unsigned i;
 if(!c || timeout<0 || retro_atomic_load_acquire_int(&c->stopped)) return 0;
 if(!retro_atomic_cas_int(&c->state,CONTROL_IDLE,CONTROL_ACQUIRE))
  return retro_atomic_load_acquire_int(&c->state)==CONTROL_HELD;
 c->owned=0;
 xe_state_budget_begin(&budget,c->clock(c->context),timeout);
 for(i=0;i<c->count;++i) {
  if(retro_atomic_load_acquire_int(&c->stopped)) break;
  if(!c->stages[i].paused(c->context)) c->owned|=UINT32_C(1)<<i;
  if(!c->stages[i].acquire(c->context,xe_state_budget_left(&budget,c->clock(c->context)))) break;
 }
 if(i==c->count && !retro_atomic_load_acquire_int(&c->stopped)) {
  retro_atomic_store_release_int(&c->state,CONTROL_HELD);return 1;
 }
 retro_atomic_store_release_int(&c->state,CONTROL_RELEASE);unwind(c);return 0;
}
int xe_state_control_release(xe_state_control *c) {
 if(!c) return 0;
 if(!retro_atomic_cas_int(&c->state,CONTROL_HELD,CONTROL_RELEASE))
  return retro_atomic_load_acquire_int(&c->state)==CONTROL_IDLE;
 unwind(c);return 1;
}
int xe_state_control_held(const xe_state_control *c) {
 return c && retro_atomic_load_acquire_int(&c->state)==CONTROL_HELD;
}
int xe_state_control_busy(const xe_state_control *c) {
 int state=c?retro_atomic_load_acquire_int(&c->state):CONTROL_IDLE;
 return state==CONTROL_ACQUIRE || state==CONTROL_RELEASE;
}
void xe_state_control_stop(xe_state_control *c) {
 if(c) retro_atomic_store_release_int(&c->stopped,1);
}
