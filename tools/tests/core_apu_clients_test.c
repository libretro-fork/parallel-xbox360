/* Packed generation/credits, retirement ownership and publication. MSVC C89. */
#include "libretro/core_apu_clients.h"
#include <stdio.h>
#include <stdlib.h>
#include <retro_atomic.h>
#include <rthreads/rthreads.h>
#include <rthreads/retro_eventcount.h>
#include <features/features_cpu.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"apu life line %u\n",(unsigned)__LINE__);exit(1);}} while(0)
static xe_apu_clients *table;
static retro_atomic_int_t hook_on,entered,allowed,stop_worker,failed;
static retro_eventcount_t event;
static uint64_t payload[8];
static retro_atomic_int_t clock_reads;
retro_time_t xe_apu_test_time(void) {retro_atomic_fetch_add_int(&clock_reads,1);return cpu_features_get_time_usec();}
static int wait_flag(retro_atomic_int_t *flag) {
  int key;int64_t start=cpu_features_get_time_usec(),elapsed;
  while(!retro_atomic_load_acquire_int(flag)) {
    key=retro_eventcount_prepare_wait(&event);
    if(retro_atomic_load_acquire_int(flag)) {retro_eventcount_cancel_wait(&event);return 1;}
    elapsed=cpu_features_get_time_usec()-start;
    if(elapsed>=2000000) {retro_eventcount_cancel_wait(&event);return 0;}
    retro_eventcount_commit_wait_timeout(&event,key,2000000-elapsed);
  }
  return 1;
}
void xe_apu_clients_test_wait_window(void) {
  if(!retro_atomic_load_acquire_int(&hook_on)) return;
  retro_atomic_store_release_int(&entered,1);retro_eventcount_notify(&event);
  CHECK(wait_flag(&allowed));
}
static uint64_t waiter_token;
static unsigned waiter_index;
static int waiter_result;
static void waiter(void *opaque) {
  (void)opaque;waiter_result=xe_apu_clients_wait(table,waiter_index,waiter_token,-1);
}
static void basic(void) {
  unsigned i,n,index,credits,before;uint64_t token,tokens[8],lease;
  xe_apu_credit_sink sink;
  sthread_t *thread;
  table=xe_apu_clients_new(8);CHECK(table);
  before=(unsigned)retro_atomic_load_acquire_int(&clock_reads);
  for(i=0;i<8;++i) {
    CHECK(xe_apu_clients_reserve(table,&index,&tokens[i]) && index==i);
    CHECK(!xe_apu_clients_active(table,i,&token));
    CHECK(!xe_apu_clients_publish(table,i,tokens[i],9));
    CHECK(xe_apu_clients_publish(table,i,tokens[i],8));
  }
  CHECK(!xe_apu_clients_reserve(table,&index,&token));
  sink.owner=table;sink.index=0;sink.token=tokens[0];
  CHECK(!xe_apu_clients_return_credit(&sink));
  for(n=0;n<8;++n) {
    CHECK(xe_apu_clients_acquire(table,0,&lease));CHECK(lease==sink.token);
    CHECK(!xe_apu_clients_acquire(table,0,&token));
    CHECK(xe_apu_clients_release(table,0,lease)==0);
  }
  CHECK(!xe_apu_clients_acquire(table,0,&lease));
  CHECK(xe_apu_clients_credits(table,0,sink.token,&credits) && !credits);
  CHECK(xe_apu_clients_return_credit(&sink));
  CHECK(xe_apu_clients_acquire(table,0,&lease));
  CHECK(xe_apu_clients_retire(table,0,&token)==XE_APU_RETIRE_WAIT);
  CHECK(!xe_apu_clients_wait(table,0,token,0));CHECK(!xe_apu_clients_return_credit(&sink));
  CHECK((unsigned)retro_atomic_load_acquire_int(&clock_reads)==before);
  waiter_index=0;waiter_token=token;
  retro_atomic_store_release_int(&hook_on,1);
  thread=sthread_create(waiter,NULL);CHECK(thread);CHECK(wait_flag(&entered));
  CHECK(xe_apu_clients_release(table,0,lease)==1);
  CHECK(xe_apu_clients_wait_callback(table,0,lease,0));
  CHECK(!xe_apu_clients_wait(table,0,lease,0));
  CHECK(!xe_apu_clients_finish(table,0,lease));
  CHECK(xe_apu_clients_claim_cleanup(table,0,lease));
  CHECK(!xe_apu_clients_claim_cleanup(table,0,lease));
  CHECK(xe_apu_clients_finish(table,0,lease));
  CHECK(xe_apu_clients_reserve(table,&index,&token) && index==0 && token!=sink.token);
  CHECK(xe_apu_clients_publish(table,0,token,8));
  retro_atomic_store_release_int(&allowed,1);retro_eventcount_notify(&event);
  sthread_join(thread);CHECK(waiter_result);retro_atomic_store_release_int(&hook_on,0);
  CHECK(xe_apu_clients_release(table,0,sink.token)==-1);
  CHECK(!xe_apu_clients_return_credit(&sink));
  for(i=0;i<8;++i) {
    CHECK(xe_apu_clients_retire(table,i,&token)==XE_APU_RETIRE_OWN);
    CHECK(xe_apu_clients_finish(table,i,token));
  }
  CHECK(xe_apu_clients_test_seed(table,0,UINT64_MAX&~UINT64_C(4095)));
  CHECK(xe_apu_clients_reserve(table,&index,&token) && index==0);
  CHECK(xe_apu_clients_publish(table,index,token,1));
  CHECK(xe_apu_clients_retire(table,index,&lease)==XE_APU_RETIRE_OWN);
  CHECK(xe_apu_clients_finish(table,index,lease));
  CHECK(xe_apu_clients_wait(table,index,token,0));
  CHECK(xe_apu_clients_reserve(table,&index,&token) && !token);
  CHECK(xe_apu_clients_abandon(table,index,token));
  CHECK(!xe_apu_clients_publish(table,index,token,8));
  CHECK(xe_apu_clients_reserve(table,&index,&token));
  CHECK(!xe_apu_clients_wait(table,index,token,1000));CHECK((unsigned)retro_atomic_load_acquire_int(&clock_reads)>before);
  CHECK(xe_apu_clients_abandon(table,index,token));
  xe_apu_clients_free(table);
}
static void callback_worker(void *unused) {
  unsigned i;uint64_t token;int action;
  (void)unused;
  while(!retro_atomic_load_acquire_int(&stop_worker)) for(i=0;i<8;++i) {
    if(!xe_apu_clients_acquire(table,i,&token)) continue;
    if(payload[i]!=token) retro_atomic_store_release_int(&failed,1);
    action=xe_apu_clients_release(table,i,token);
    if(action<0) retro_atomic_store_release_int(&failed,1);
  }
}
static void registrations(void *unused) {
  unsigned round,index;uint64_t token;int action;xe_apu_credit_sink sink;
  (void)unused;
  for(round=0;round<2000;++round) {
    while(!xe_apu_clients_reserve(table,&index,&token)) {if(retro_atomic_load_acquire_int(&failed)) return;}
    payload[index]=token;
    if(!xe_apu_clients_publish(table,index,token,8)) {retro_atomic_store_release_int(&failed,1);return;}
    action=xe_apu_clients_retire(table,index,&token);
    if(action==XE_APU_RETIRE_OWN) {payload[index]=0;CHECK(xe_apu_clients_finish(table,index,token));}
    else {
      CHECK(action==XE_APU_RETIRE_WAIT && xe_apu_clients_wait_callback(table,index,token,2000000));
      CHECK(xe_apu_clients_claim_cleanup(table,index,token));payload[index]=0;
      CHECK(xe_apu_clients_finish(table,index,token));
    }
    sink.owner=table;sink.index=index;sink.token=token;
    if(xe_apu_clients_return_credit(&sink)) retro_atomic_store_release_int(&failed,1);
  }
}
int main(void) {
  static const unsigned limits[]={1,4,8,64};
  sthread_t *worker,*writers[4];unsigned i,n,index,credits;uint64_t token,lease;
  xe_apu_credit_sink sink;
  CHECK(!xe_apu_clients_new(0) && !xe_apu_clients_new(65));
  CHECK(retro_eventcount_init(&event));basic();
  for(i=0;i<4;++i) {
    table=xe_apu_clients_new(limits[i]);CHECK(table);
    CHECK(xe_apu_clients_reserve(table,&index,&token));
    CHECK(xe_apu_clients_publish(table,index,token,limits[i]));
    sink.owner=table;sink.index=index;sink.token=token;
    CHECK(!xe_apu_clients_return_credit(&sink));
    for(n=0;n<limits[i];++n) {
      CHECK(xe_apu_clients_acquire(table,index,&lease));
      CHECK(xe_apu_clients_release(table,index,lease)==0);
    }
    CHECK(!xe_apu_clients_acquire(table,index,&lease));
    for(n=0;n<limits[i];++n) CHECK(xe_apu_clients_return_credit(&sink));
    CHECK(xe_apu_clients_credits(table,index,token,&credits) && credits==limits[i]);
    CHECK(xe_apu_clients_retire(table,index,&token)==XE_APU_RETIRE_OWN);
    CHECK(xe_apu_clients_finish(table,index,token));xe_apu_clients_free(table);
  }
  table=xe_apu_clients_new(8);CHECK(table);
  worker=sthread_create(callback_worker,NULL);CHECK(worker);
  for(i=0;i<4;++i) {writers[i]=sthread_create(registrations,NULL);CHECK(writers[i]);}
  for(i=0;i<4;++i) sthread_join(writers[i]);
  retro_atomic_store_release_int(&stop_worker,1);sthread_join(worker);
  CHECK(!retro_atomic_load_acquire_int(&failed));xe_apu_clients_free(table);retro_eventcount_free(&event);
  puts("PASS: packed callback/credits, 8000 concurrent lifetimes, deferred cleanup, epoch reuse/wrap and notification-before-wait");return 0;
}
