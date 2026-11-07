#include "core_apu_clients.h"
#include <stdlib.h>
#include <retro_atomic.h>
#include <rthreads/retro_eventcount.h>
#include <features/features_cpu.h>
#if !defined(RETRO_ATOMIC_HAS_64) || !defined(RETRO_ATOMIC_LOCK_FREE)
#error Lock-free 64-bit common atomics are required.
#endif
enum {BUILD=1,ACTIVE=2,LEASE=4,RETIRING=8,READY=16,OWNED=17,FLAGS=31,CREDIT_SHIFT=5};
#define LOW_MASK UINT64_C(4095)
#define NEXT_GENERATION UINT64_C(4096)
struct xe_apu_clients {
  retro_atomic_64_t slots[XE_APU_CLIENT_COUNT];
  retro_eventcount_t done;
  unsigned max_credits;
};
static uint64_t load_slot(const xe_apu_clients *t,unsigned i) {
  return (uint64_t)retro_atomic_load_acquire_64(&t->slots[i]);
}
static int valid(const xe_apu_clients *t,unsigned i,uint64_t token) {
  return t && i<XE_APU_CLIENT_COUNT && !(token&LOW_MASK);
}
static unsigned credit_count(uint64_t word) {return (unsigned)((word&LOW_MASK)>>CREDIT_SHIFT);}
static int change(xe_apu_clients *t,unsigned i,uint64_t old,uint64_t next) {
  return retro_atomic_cas_64(&t->slots[i],(int64_t)old,(int64_t)next);
}
xe_apu_clients *xe_apu_clients_new(unsigned max_credits) {
  xe_apu_clients *t;unsigned i;
  if(!max_credits || max_credits>64) return NULL;
  t=(xe_apu_clients *)calloc(1,sizeof(*t));if(!t) return NULL;
  for(i=0;i<XE_APU_CLIENT_COUNT;++i) retro_atomic_64_init(&t->slots[i],0);
  if(!retro_eventcount_init(&t->done)) {free(t);return NULL;}
  t->max_credits=max_credits;return t;
}
void xe_apu_clients_free(xe_apu_clients *t) {
  if(t) {retro_eventcount_free(&t->done);free(t);}
}
int xe_apu_clients_reserve(xe_apu_clients *t,unsigned *index,uint64_t *token) {
  unsigned i;uint64_t old;
  if(!t || !index || !token) return 0;
  for(i=0;i<XE_APU_CLIENT_COUNT;++i) {
    old=load_slot(t,i);
    if((old&LOW_MASK) || !change(t,i,old,old|BUILD)) continue;
    *index=i;*token=old;return 1;
  }
  return 0;
}
int xe_apu_clients_publish(xe_apu_clients *t,unsigned i,uint64_t token,unsigned credits) {
  return valid(t,i,token) && credits<=t->max_credits &&
      change(t,i,token|BUILD,token|ACTIVE|((uint64_t)credits<<CREDIT_SHIFT));
}
int xe_apu_clients_abandon(xe_apu_clients *t,unsigned i,uint64_t token) {
  if(!valid(t,i,token) || !change(t,i,token|BUILD,token+NEXT_GENERATION)) return 0;
  retro_eventcount_notify(&t->done);return 1;
}
int xe_apu_clients_active(const xe_apu_clients *t,unsigned i,uint64_t *token) {
  uint64_t old;unsigned flags;
  if(!t || i>=XE_APU_CLIENT_COUNT || !token) return 0;
  old=load_slot(t,i);flags=(unsigned)(old&FLAGS);
  if(flags!=ACTIVE && flags!=(ACTIVE|LEASE)) return 0;
  *token=old&~LOW_MASK;return 1;
}
int xe_apu_clients_acquire(xe_apu_clients *t,unsigned i,uint64_t *token) {
  uint64_t old,next;
  if(!t || i>=XE_APU_CLIENT_COUNT || !token) return 0;
  for(;;) {
    old=load_slot(t,i);
    if((old&FLAGS)!=ACTIVE || !credit_count(old)) return 0;
    next=(old-(UINT64_C(1)<<CREDIT_SHIFT))|LEASE;
    if(change(t,i,old,next)) {*token=old&~LOW_MASK;return 1;}
  }
}
int xe_apu_clients_release(xe_apu_clients *t,unsigned i,uint64_t token) {
  uint64_t old,next;unsigned flags;
  if(!valid(t,i,token)) return -1;
  for(;;) {
    old=load_slot(t,i);flags=(unsigned)(old&FLAGS);
    if((old&~LOW_MASK)!=token || (flags!=(ACTIVE|LEASE) && flags!=(RETIRING|LEASE))) return -1;
    next=(old&~(uint64_t)FLAGS)|(flags==(ACTIVE|LEASE)?ACTIVE:READY);
    if(change(t,i,old,next)) {
      if(flags==(ACTIVE|LEASE)) return 0;
      retro_eventcount_notify(&t->done);return 1;
    }
  }
}
int xe_apu_clients_retire(xe_apu_clients *t,unsigned i,uint64_t *token) {
  uint64_t old,next;unsigned flags;
  if(!t || i>=XE_APU_CLIENT_COUNT || !token) return XE_APU_RETIRE_NONE;
  for(;;) {
    old=load_slot(t,i);flags=(unsigned)(old&FLAGS);*token=old&~LOW_MASK;
    if(flags==(RETIRING|LEASE) || flags==OWNED) return XE_APU_RETIRE_WAIT;
    if(flags!=ACTIVE && flags!=(ACTIVE|LEASE) && flags!=READY) return XE_APU_RETIRE_NONE;
    next=(old&~(uint64_t)FLAGS)|(flags==(ACTIVE|LEASE)?(RETIRING|LEASE):OWNED);
    if(change(t,i,old,next)) return flags==(ACTIVE|LEASE)?XE_APU_RETIRE_WAIT:XE_APU_RETIRE_OWN;
  }
}
static int claim_cleanup(xe_apu_clients *t,unsigned i,uint64_t *token,int exact) {
  uint64_t old,next;
  if(!t || i>=XE_APU_CLIENT_COUNT || !token) return 0;
  for(;;) {
    old=load_slot(t,i);
    if((old&FLAGS)!=READY || (exact && (old&~LOW_MASK)!=*token)) return 0;
    next=(old&~(uint64_t)FLAGS)|OWNED;
    if(change(t,i,old,next)) {*token=old&~LOW_MASK;return 1;}
  }
}
int xe_apu_clients_claim_cleanup(xe_apu_clients *t,unsigned i,uint64_t token) {
  return valid(t,i,token) && claim_cleanup(t,i,&token,1);
}
int xe_apu_clients_collect(xe_apu_clients *t,unsigned i,uint64_t *token) {
  return claim_cleanup(t,i,token,0);
}
int xe_apu_clients_finish(xe_apu_clients *t,unsigned i,uint64_t token) {
  uint64_t old;
  if(!valid(t,i,token)) return 0;
  for(;;) {
    old=load_slot(t,i);
    if((old&~LOW_MASK)!=token || (old&FLAGS)!=OWNED) return 0;
    if(change(t,i,old,token+NEXT_GENERATION)) break;
  }
  retro_eventcount_notify(&t->done);return 1;
}
int xe_apu_clients_return_credit(const xe_apu_credit_sink *sink) {
  uint64_t old;unsigned flags;
  if(!sink || !valid(sink->owner,sink->index,sink->token)) return 0;
  for(;;) {
    old=load_slot(sink->owner,sink->index);flags=(unsigned)(old&FLAGS);
    if((old&~LOW_MASK)!=sink->token || (flags!=ACTIVE && flags!=(ACTIVE|LEASE)) ||
        credit_count(old)>=sink->owner->max_credits) return 0;
    if(change(sink->owner,sink->index,old,old+(UINT64_C(1)<<CREDIT_SHIFT))) return 1;
  }
}
int xe_apu_clients_credits(const xe_apu_clients *t,unsigned i,uint64_t token,unsigned *credits) {
  uint64_t old;
  if(!valid(t,i,token) || !credits) return 0;
  old=load_slot(t,i);
  if((old&~LOW_MASK)!=token) return 0;
  *credits=credit_count(old);return 1;
}
static int complete(const xe_apu_clients *t,unsigned i,uint64_t token,int callback) {
  uint64_t old=load_slot(t,i);
  return (old&~LOW_MASK)!=token ||
      (callback && ((old&FLAGS)==READY || (old&FLAGS)==OWNED));
}
#ifdef XE_APU_CLIENT_TEST
extern void xe_apu_clients_test_wait_window(void);
#endif
static int wait_client(xe_apu_clients *t,unsigned i,uint64_t token,int64_t timeout_us,int callback) {
  uint64_t start=0,elapsed;int key;int64_t remaining;
  if(!valid(t,i,token) || timeout_us < -1) return 0;
  if(timeout_us>0) start=(uint64_t)cpu_features_get_time_usec();
  for(;;) {
    if(complete(t,i,token,callback)) return 1;
    if(!timeout_us) return 0;
    key=retro_eventcount_prepare_wait(&t->done);
    if(complete(t,i,token,callback)) {retro_eventcount_cancel_wait(&t->done);return 1;}
#ifdef XE_APU_CLIENT_TEST
    xe_apu_clients_test_wait_window();
#endif
    if(timeout_us<0) retro_eventcount_commit_wait(&t->done,key);
    else {
      elapsed=(uint64_t)cpu_features_get_time_usec()-start;
      if(elapsed>=(uint64_t)timeout_us) {retro_eventcount_cancel_wait(&t->done);return complete(t,i,token,callback);}
      remaining=timeout_us-(int64_t)elapsed;
      retro_eventcount_commit_wait_timeout(&t->done,key,remaining);
    }
  }
}
int xe_apu_clients_wait(xe_apu_clients *t,unsigned i,uint64_t token,int64_t timeout_us) {
  return wait_client(t,i,token,timeout_us,0);
}
int xe_apu_clients_wait_callback(xe_apu_clients *t,unsigned i,uint64_t token,int64_t timeout_us) {
  return wait_client(t,i,token,timeout_us,1);
}
#ifdef XE_APU_CLIENT_TEST
int xe_apu_clients_test_seed(xe_apu_clients *t,unsigned i,uint64_t token) {
  if(!valid(t,i,token) || (load_slot(t,i)&LOW_MASK)) return 0;
  retro_atomic_store_release_64(&t->slots[i],(int64_t)token);return 1;
}
#endif
