/* C89 event oracle, real bounded queue and move-ownership negative controls. */
#include "libretro/core_draw_stats.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"draw stats line %u\n",(unsigned)__LINE__);exit(1);}}while(0)
static unsigned api_calls;
static int fake_api(void) {++api_calls;return 19;}
#if XE_GPU_DRAW_STATS
#include "libretro/core_job_pool.h"
#include "draw_contract_bridge.h"
#include <retro_atomic.h>
#include <rthreads/rthreads.h>
#include <rthreads/retro_eventcount.h>
#include <features/features_cpu.h>
static xe_draw_stats snapshot;
static void capture(void){CHECK(xe_draw_stats_snapshot(&snapshot,sizeof(snapshot)));}
struct fixture {
 retro_atomic_int_t entered,release,stores;
 retro_eventcount_t changed;
 unsigned seen[4097];
 xe_job_pool *pool;
};
static void wait_count(struct fixture* f,retro_atomic_int_t* value,int count){
 int key;int64_t left,end=cpu_features_get_time_usec()+5000000;
 while(retro_atomic_load_acquire_int(value)!=count){
  key=retro_eventcount_prepare_wait(&f->changed);left=end-cpu_features_get_time_usec();CHECK(left>0);
  if(retro_atomic_load_acquire_int(value)!=count)retro_eventcount_commit_wait_timeout(&f->changed,key,left);
  else retro_eventcount_cancel_wait(&f->changed);
 }
}
static uintptr_t create(void* opaque,size_t id){
 struct fixture* f=(struct fixture*)opaque;CHECK(id<4097);CHECK(xe_draw_stats_role()==XE_DRAW_COMPILER);
 if(!id){retro_atomic_store_release_int(&f->entered,1);retro_eventcount_notify(&f->changed);wait_count(f,&f->release,1);}
 return (uintptr_t)id+1;
}
static void store(void* opaque,size_t id,uintptr_t result){
 struct fixture* f=(struct fixture*)opaque;CHECK(result==id+1 && !f->seen[id]++);
 retro_atomic_fetch_add_int(&f->stores,1);retro_eventcount_notify(&f->changed);
}
static void counter_worker(void* opaque){unsigned i,previous;(void)opaque;previous=xe_draw_stats_enter(XE_DRAW_COMPILER);
 for(i=0;i<20000;++i)XE_DRAW_EVENT(XE_DRAW_UTILITY_PIPELINE,XE_DRAW_GRAPHICS_PSO);
 xe_draw_stats_leave(previous);
}
static void finish(void* opaque,void* payload,void* context,uint64_t lease){struct fixture* f=(struct fixture*)opaque;(void)context;CHECK(*(unsigned*)payload<3);CHECK(xe_job_pool_finish(f->pool,lease));}
static void discard(void* opaque,void* payload){(void)opaque;(void)payload;}
static void blocked_producer(void* opaque){
 struct fixture* f=(struct fixture*)opaque;unsigned* value;unsigned previous=xe_draw_stats_enter(XE_DRAW_GPU);
 value=(unsigned*)xe_job_pool_begin(f->pool);CHECK(value);*value=2;xe_job_pool_commit(f->pool);xe_draw_stats_leave(previous);
}
static void check_queue(void){
 struct fixture f;struct draw_callbacks callbacks;void* queue;size_t id;unsigned i,moves=0,previous;
 memset(&f,0,sizeof(f));CHECK(retro_eventcount_init(&f.changed));callbacks.owner=&f;callbacks.create=create;callbacks.store=store;
 queue=draw_queue_new(&callbacks);CHECK(queue);previous=xe_draw_stats_enter(XE_DRAW_GPU);CHECK(draw_scope_unwind());xe_draw_stats_reset();
 id=123;CHECK(!draw_queue_try(queue,&id,&moves) && id==123 && !moves);
 draw_queue_threads(queue,1);id=0;CHECK(draw_queue_try(queue,&id,&moves));wait_count(&f,&f.entered,1);
 for(i=1;i<4096;++i){id=i;CHECK(draw_queue_try(queue,&id,&moves) && !id);}
 CHECK(moves==4096);id=4096;CHECK(!draw_queue_try(queue,&id,&moves) && id==4096 && moves==4096);
 capture();CHECK(xe_draw_stats_violations(&snapshot)==0); /* no wait or inline work on full */
 retro_atomic_store_release_int(&f.release,1);retro_eventcount_notify(&f.changed);wait_count(&f,&f.stores,4096);
 CHECK(draw_queue_try(queue,&id,&moves) && !id && moves==4097);wait_count(&f,&f.stores,4097);
 draw_queue_threads(queue,0);id=77;CHECK(!draw_queue_try(queue,&id,&moves) && id==77 && moves==4097);
 draw_queue_free(queue);capture();CHECK(!xe_draw_stats_violations(&snapshot));xe_draw_stats_leave(previous);retro_eventcount_free(&f.changed);
}
static void check_blocking_oracle(void){
 struct fixture f;unsigned i,*payload,previous;sthread_t* thread;int64_t end;
 memset(&f,0,sizeof(f));f.pool=xe_job_pool_new(2,sizeof(unsigned),"Blocked admission",finish,discard,NULL,NULL,NULL,&f);CHECK(f.pool);
 previous=xe_draw_stats_enter(XE_DRAW_INITIALIZATION);xe_draw_stats_reset();
 CHECK(!xe_job_pool_try_begin(NULL));
 for(i=0;i<2;++i){payload=(unsigned*)xe_job_pool_try_begin(f.pool);CHECK(payload);*payload=i;xe_job_pool_commit(f.pool);}
 for(i=0;i<10000;++i){CHECK(!xe_job_pool_try_begin(f.pool));}
 capture();CHECK(!xe_draw_stats_violations(&snapshot));
 thread=sthread_create(blocked_producer,&f);CHECK(thread);end=cpu_features_get_time_usec()+5000000;
 do{capture();CHECK(cpu_features_get_time_usec()<end);}while(!snapshot.events[XE_DRAW_GPU][XE_DRAW_GUEST_PIPELINE][XE_DRAW_ADMISSION_WAIT]);
 CHECK(xe_job_pool_run_one(f.pool,NULL));sthread_join(thread);while(xe_job_pool_run_one(f.pool,NULL)){}
 capture();CHECK(xe_draw_stats_violations(&snapshot)>0);xe_job_pool_free(f.pool);xe_draw_stats_leave(previous);
}
int main(int argc,char** argv){
 unsigned r,k,e,previous,i;sthread_t* threads[4];int value;
 (void)argv;xe_draw_stats_reset();CHECK(!xe_draw_stats_snapshot(NULL,sizeof(snapshot)));CHECK(!xe_draw_stats_snapshot(&snapshot,sizeof(snapshot)-1));CHECK(!xe_draw_stats_snapshot(&snapshot,sizeof(snapshot)+1));
 CHECK(xe_draw_stats_violations(NULL)==(size_t)-1);
 for(r=0;r<XE_DRAW_ROLE_COUNT;++r){previous=xe_draw_stats_enter(r);for(k=0;k<XE_DRAW_KIND_COUNT;++k)for(e=0;e<XE_DRAW_EVENT_COUNT;++e)XE_DRAW_EVENT(k,e);xe_draw_stats_leave(previous);}
 capture();CHECK(xe_draw_stats_violations(&snapshot)==4*XE_DRAW_KIND_COUNT*XE_DRAW_EVENT_COUNT);
 for(r=0;r<XE_DRAW_ROLE_COUNT;++r)for(k=0;k<XE_DRAW_KIND_COUNT;++k)for(e=0;e<XE_DRAW_EVENT_COUNT;++e)CHECK(snapshot.events[r][k][e]==1);
 if(argc>1){puts("EXPECTED FAILURE: critical/unknown event gate rejected injected calls");return xe_draw_stats_violations(&snapshot)?1:0;}
 snapshot.abi_version=0;CHECK(xe_draw_stats_violations(&snapshot)==(size_t)-1);
 xe_draw_stats_reset();for(i=0;i<4;++i){threads[i]=sthread_create(counter_worker,NULL);CHECK(threads[i]);}for(i=0;i<4;++i)sthread_join(threads[i]);
 capture();CHECK(!xe_draw_stats_violations(&snapshot));CHECK(snapshot.events[XE_DRAW_COMPILER][XE_DRAW_UTILITY_PIPELINE][XE_DRAW_GRAPHICS_PSO]==80000);
 previous=xe_draw_stats_enter(XE_DRAW_FRONTEND);value=XE_DRAW_CALL(XE_DRAW_UTILITY_PIPELINE,XE_DRAW_MODULE,fake_api());xe_draw_stats_leave(previous);CHECK(value==19 && api_calls==1);
 capture();CHECK(xe_draw_stats_violations(&snapshot)==1);previous=xe_draw_stats_enter(999);XE_DRAW_EVENT(999,999);xe_draw_stats_leave(previous);capture();CHECK(xe_draw_stats_violations(&snapshot)==2);
 check_queue();check_blocking_oracle();puts("PASS: C89 by-role/kind events, ABI bounds, 80000 atomic events, nonblocking full/no-worker/wrap ownership; blocking admission rejected");return 0;
}
#else
int main(void){unsigned effects=0;int result;
 XE_DRAW_SCOPE(++effects);XE_DRAW_EVENT(++effects,++effects);
 result=XE_DRAW_CALL(++effects,++effects,fake_api());CHECK(!effects && api_calls==1 && result==19);
 puts("PASS: release macros evaluate only the original API call; no counter dependency");return 0;
}
#endif
