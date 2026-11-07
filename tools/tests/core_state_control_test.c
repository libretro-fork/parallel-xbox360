/* Borrowed protocols, rollback and cancellation. MSVC C89. */
#include "libretro/core_state_control.h"
#include <rthreads/rthreads.h>
#include <features/features_cpu.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)) {fprintf(stderr,"control line %u\n",(unsigned)__LINE__);exit(1);}} while(0)
typedef struct fixture {
 xe_state_control control;
 int paused[3],fail,fail_before,reenter;
 int64_t now,received[3];
 unsigned calls[3],release[3],order[8],released;
 retro_atomic_int_t entered,proceed;
 int hold,cost;
} fixture;
static int64_t clock_value(void *p) {return ((fixture*)p)->now;}
static int paused(fixture *f,unsigned i) {return f->paused[i];}
static int acquire(fixture *f,unsigned i,int64_t remaining) {
 CHECK(xe_state_control_busy(&f->control) && !xe_state_control_held(&f->control));
 if(f->reenter) {CHECK(!xe_state_control_acquire(&f->control,0));CHECK(!xe_state_control_release(&f->control));}
 ++f->calls[i];f->received[i]=remaining;f->now+=f->cost;
 if(f->fail_before && f->fail==(int)i) return 0;
 f->paused[i]=1;
 if(f->hold && i==1) {
  retro_atomic_store_release_int(&f->entered,1);
  while(!retro_atomic_load_acquire_int(&f->proceed)) {}
 }
 return f->fail!=(int)i;
}
static void release(fixture *f,unsigned i) {
 CHECK(xe_state_control_busy(&f->control));
 CHECK(!xe_state_control_release(&f->control));
 CHECK(!xe_state_control_acquire(&f->control,0));
 f->paused[i]=0;++f->release[i];f->order[f->released++]=i;
}
#define STAGE(n) static int paused##n(void*p){return paused((fixture*)p,n);} \
 static int acquire##n(void*p,int64_t t){return acquire((fixture*)p,n,t);} \
 static void release##n(void*p){release((fixture*)p,n);}
STAGE(0) STAGE(1) STAGE(2)
static const xe_state_stage stages[3]={{paused0,acquire0,release0},{paused1,acquire1,release1},{paused2,acquire2,release2}};
static void initialize(fixture *f) {
 memset(f,0,sizeof(*f));f->fail=-1;f->now=100;f->cost=10;
 CHECK(xe_state_control_init(&f->control,stages,3,f,clock_value));
 CHECK(f->control.stages==stages);
 retro_atomic_int_init(&f->entered,0);retro_atomic_int_init(&f->proceed,0);
}
static void stop_during_acquire(void *p) {
 fixture *f=(fixture*)p;
 int64_t start=cpu_features_get_time_usec();
 while(!retro_atomic_load_acquire_int(&f->entered))
  CHECK((uint64_t)cpu_features_get_time_usec()-(uint64_t)start<UINT64_C(2000000));
 CHECK(xe_state_control_busy(&f->control));
 xe_state_control_stop(&f->control);
 retro_atomic_store_release_int(&f->proceed,1);
}
static int never_paused(void *p) {(void)p;return 0;}
static int accept_stage(void *p,int64_t t) {fixture *f=(fixture*)p;(void)t;++f->calls[0];return 1;}
static void release_stage(void *p) {++((fixture*)p)->release[0];}
int main(void) {
 fixture f;
 unsigned mask,i,j,fail;
 xe_state_stage wide[32];
 sthread_t *thread;
 CHECK(!xe_state_control_acquire(NULL,0) && !xe_state_control_release(NULL));
 CHECK(!xe_state_control_held(NULL) && !xe_state_control_busy(NULL));xe_state_control_stop(NULL);
 initialize(&f);
 CHECK(!xe_state_control_init(&f.control,stages,33,&f,clock_value));
 CHECK(!xe_state_control_acquire(&f.control,-1) && !f.calls[0]);
 for(mask=0;mask<8;++mask) for(fail=0;fail<4;++fail) for(j=0;j<2;++j) {
  initialize(&f);f.fail=fail==3?-1:(int)fail;f.fail_before=j;f.reenter=1;
  for(i=0;i<3;++i) f.paused[i]=(mask>>i)&1;
  if(fail==3) {
   CHECK(xe_state_control_acquire(&f.control,35));CHECK(xe_state_control_held(&f.control));
   CHECK(f.received[0]==35 && f.received[1]==25 && f.received[2]==15);
   CHECK(xe_state_control_acquire(&f.control,1));CHECK(f.calls[0]==1);
   CHECK(xe_state_control_release(&f.control));
  } else CHECK(!xe_state_control_acquire(&f.control,35));
  CHECK(!xe_state_control_busy(&f.control) && !xe_state_control_held(&f.control));
  for(i=0;i<3;++i) {
   CHECK(f.paused[i]==(int)((mask>>i)&1));
   CHECK(f.release[i]==(unsigned)(i<=(fail==3?2:fail) && !(mask&(1u<<i))));
  }
  for(i=1;i<f.released;++i) CHECK(f.order[i-1]>f.order[i]);
  CHECK(xe_state_control_release(&f.control));
 }
 for(i=0;i<256;++i) {
  initialize(&f);f.hold=1;
  thread=sthread_create(stop_during_acquire,&f);CHECK(thread);
  CHECK(!xe_state_control_acquire(&f.control,100));sthread_join(thread);
  CHECK(!f.paused[0] && !f.paused[1] && !f.calls[2]);
  CHECK(!xe_state_control_acquire(&f.control,100));
 }
 initialize(&f);f.cost=0;CHECK(xe_state_control_acquire(&f.control,0));
 CHECK(!f.received[0] && !f.received[1] && !f.received[2]);
 xe_state_control_stop(&f.control);CHECK(xe_state_control_held(&f.control));
 CHECK(xe_state_control_release(&f.control));CHECK(!xe_state_control_acquire(&f.control,0));
 initialize(&f);
 for(i=0;i<10000;++i) {
  f.released=0;
  CHECK(xe_state_control_acquire(&f.control,35));CHECK(xe_state_control_release(&f.control));
 }
 CHECK(f.calls[0]==10000 && f.release[0]==10000);
 for(i=0;i<32;++i) {wide[i].paused=never_paused;wide[i].acquire=accept_stage;wide[i].release=release_stage;}
 initialize(&f);CHECK(xe_state_control_init(&f.control,wide,32,&f,clock_value));
 CHECK(xe_state_control_acquire(&f.control,0) && f.control.owned==UINT32_MAX);
 CHECK(xe_state_control_release(&f.control));CHECK(f.calls[0]==32 && f.release[0]==32);
 printf("PASS: borrowed stages, 64 failure/prepause cases, 10000 reuse cycles, 32-stage mask, 256 stop races; %lu controller bytes\n",(unsigned long)sizeof(xe_state_control));return 0;
}
