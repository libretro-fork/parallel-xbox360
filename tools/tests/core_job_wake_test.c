/* Deterministic compiler idle registration and targeted wake tests. C89. */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <retro_atomic.h>
#include <rthreads/rthreads.h>
#include <rthreads/retro_eventcount.h>
#include "libretro/core_job_pool.h"
static void job_park_hook(size_t,unsigned);
#define XE_JOB_PARK_HOOK(index,phase) job_park_hook(index,phase)
#include "libretro/core_job_pool.c"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"job wake line %u\n",(unsigned)__LINE__);exit(1);}}while(0)
static retro_eventcount_t gate;
static retro_atomic_int_t held,release_park,notified,processed,entered,release_work,wakes,seen[8];
static unsigned hold_phase,hold_work;
static xe_job_pool* current;
static void await_value(retro_atomic_int_t* value,int expected){int key;
 while(retro_atomic_load_acquire_int(value)<expected){key=retro_eventcount_prepare_wait(&gate);
 if(retro_atomic_load_acquire_int(value)>=expected)retro_eventcount_cancel_wait(&gate);
 else CHECK(retro_eventcount_commit_wait_timeout(&gate,key,5000000) || retro_atomic_load_acquire_int(value)>=expected);}}
static void job_park_hook(size_t index,unsigned phase){
 if(phase==3)retro_atomic_fetch_add_int(&wakes,1);
 retro_eventcount_notify(&gate);
 if(index==0 && phase==hold_phase && retro_atomic_cas_int(&held,0,1)){
 int key;
 retro_eventcount_notify(&gate);
 while(!retro_atomic_load_acquire_int(&release_park)){key=retro_eventcount_prepare_wait(&gate);
 if(retro_atomic_load_acquire_int(&release_park))retro_eventcount_cancel_wait(&gate);
 else CHECK(retro_eventcount_commit_wait_timeout(&gate,key,10000000) || retro_atomic_load_acquire_int(&release_park));}}}
static void wake_run(void* opaque,void* payload,void* context,uint64_t lease){unsigned id=*(unsigned*)payload;(void)opaque;(void)context;
 CHECK(id<8);CHECK(retro_atomic_fetch_add_int(&seen[id],1)==0);
 retro_atomic_fetch_add_int(&entered,1);retro_eventcount_notify(&gate);
 if(hold_work)await_value(&release_work,1);
 CHECK(xe_job_pool_finish(current,lease));retro_atomic_fetch_add_int(&processed,1);retro_eventcount_notify(&gate);}
static void wake_discard(void* opaque,void* payload){(void)opaque;(void)payload;CHECK(0);}
static void reset(unsigned phase,unsigned work){unsigned i;
 hold_phase=phase;hold_work=work;
 retro_atomic_store_release_int(&held,0);retro_atomic_store_release_int(&release_park,0);
 retro_atomic_store_release_int(&notified,0);retro_atomic_store_release_int(&processed,0);
 retro_atomic_store_release_int(&entered,0);retro_atomic_store_release_int(&release_work,0);
 retro_atomic_store_release_int(&wakes,0);for(i=0;i<8;++i)retro_atomic_store_release_int(&seen[i],0);}
static void submit(unsigned id){unsigned* slot=(unsigned*)xe_job_pool_try_begin(current);CHECK(slot);*slot=id;xe_job_pool_commit(current);}
static void producer(void* opaque){(void)opaque;submit(0);retro_atomic_store_release_int(&notified,1);retro_eventcount_notify(&gate);}
static void await_parked(int);
static void parked_window(unsigned phase,unsigned stolen){unsigned round;sthread_t* thread;
 for(round=0;round<32;++round){reset(phase,0);
 current=xe_job_pool_new(16,sizeof(unsigned),"Wake boundary",wake_run,wake_discard,NULL,NULL,NULL,NULL);CHECK(current);
 CHECK(xe_job_pool_set_threads(current,1));await_value(&held,1);
 thread=sthread_create(producer,NULL);CHECK(thread);
 /* Producer must finish admission/notification while the idle worker is held. */
 await_value(&notified,1);CHECK(!retro_atomic_load_acquire_int(&processed));
 CHECK(retro_atomic_load_acquire_int(&wakes)==(phase==4?0:1));
 if(stolen){CHECK(xe_job_pool_run_one(current,NULL));CHECK(retro_atomic_load_acquire_int(&processed)==1);}
 retro_atomic_store_release_int(&release_park,1);retro_eventcount_notify(&gate);
 sthread_join(thread);
 if(stolen){await_parked(1);submit(1);await_value(&processed,2);}else await_value(&processed,1);
 CHECK(xe_job_pool_set_threads(current,0));
 CHECK(!retro_atomic_load_acquire_int(&current->parked_count));CHECK(!xe_job_pool_busy(current));xe_job_pool_free(current);}}
static void await_parked(int expected){int key;
 while(retro_atomic_load_acquire_int(&current->parked_count)!=expected){key=retro_eventcount_prepare_wait(&gate);
 if(retro_atomic_load_acquire_int(&current->parked_count)==expected)retro_eventcount_cancel_wait(&gate);
 else CHECK(retro_eventcount_commit_wait_timeout(&gate,key,5000000) || retro_atomic_load_acquire_int(&current->parked_count)==expected);}}
static void fanout(void){unsigned i;
 reset(0,1);current=xe_job_pool_new(16,sizeof(unsigned),"Targeted wake",wake_run,wake_discard,NULL,NULL,NULL,NULL);CHECK(current);
 CHECK(xe_job_pool_set_threads(current,8));await_parked(8);
 submit(0);await_value(&entered,1);await_parked(7);CHECK(retro_atomic_load_acquire_int(&wakes)<=1);
 for(i=1;i<8;++i){submit(i);}
 await_value(&entered,8);CHECK(!retro_atomic_load_acquire_int(&current->parked_count));
 CHECK(retro_atomic_load_acquire_int(&wakes)<=8);retro_atomic_store_release_int(&release_work,1);retro_eventcount_notify(&gate);
 await_value(&processed,8);await_parked(8);
 CHECK(xe_job_pool_set_threads(current,2));await_parked(2);
 CHECK(xe_job_pool_set_threads(current,6));await_parked(6);
 CHECK(xe_job_pool_set_threads(current,0));CHECK(!retro_atomic_load_acquire_int(&current->parked_count));
 CHECK(xe_job_pool_set_threads(current,4));await_parked(4);xe_job_pool_free(current);}
int main(void){CHECK(retro_eventcount_init(&gate));parked_window(4,0);parked_window(1,0);parked_window(2,0);parked_window(1,1);fanout();retro_eventcount_free(&gate);
 puts("PASS: 128 held account/check/commit and stolen-job windows preserve nonblocking notify, at-most-one selected wake per job, eight distinct workers, resize/restart and joined teardown");return 0;}
