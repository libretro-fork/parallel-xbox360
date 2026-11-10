/* Owned resource publication, cached failure and joined destruction. C89. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <retro_atomic.h>
#include <rthreads/rthreads.h>
#include <rthreads/retro_eventcount.h>
static retro_atomic_int_t denied,nodes,freed_nodes,emits,resources,disposed;
static void* node_alloc(size_t bytes){void* p;if(retro_atomic_load_acquire_int(&denied))return NULL;p=malloc(bytes);if(p)retro_atomic_fetch_add_int(&nodes,1);return p;}
static void node_free(void* p){if(p)retro_atomic_fetch_add_int(&freed_nodes,1);free(p);}
#define XE_READY_OWNED_ALLOC node_alloc
#define XE_READY_OWNED_FREE node_free
#include "libretro/core_ready_owned.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"owned ready line %u\n",(unsigned)__LINE__);exit(1);}}while(0)
static xe_ready_cache cache;
static retro_eventcount_t gate;
struct resource {uint64_t key,seal;};
struct context {uint64_t key;int fail,hold;retro_atomic_int_t entered,release;void* made;void* result;};
static void await(retro_atomic_int_t* flag){int key;
 while(!retro_atomic_load_acquire_int(flag)){key=retro_eventcount_prepare_wait(&gate);
  if(retro_atomic_load_acquire_int(flag))retro_eventcount_cancel_wait(&gate);
  else CHECK(retro_eventcount_commit_wait_timeout(&gate,key,5000000) || retro_atomic_load_acquire_int(flag));}}
static void* create(void* opaque,uint64_t key){struct context* c=(struct context*)opaque;struct resource* value=NULL;
 retro_atomic_fetch_add_int(&emits,1);
 if(!c->fail && !(key>=32 && key<96 && key%7==0)){value=(struct resource*)malloc(sizeof(*value));CHECK(value);value->key=key;value->seal=key^UINT64_C(0x12345678fedcba98);retro_atomic_fetch_add_int(&resources,1);}
 c->made=value;
 if(c->hold){retro_atomic_store_release_int(&c->entered,1);retro_eventcount_notify(&gate);await(&c->release);}
 return value;
}
static void dispose(void* opaque){struct resource* value=(struct resource*)opaque;
 CHECK(value && value->seal==(value->key^UINT64_C(0x12345678fedcba98)));retro_atomic_fetch_add_int(&disposed,1);value->seal=0;free(value);}
static void one(void* opaque){struct context* c=(struct context*)opaque;c->result=xe_ready_owned_get(&cache,c->key,create,dispose,c);}
static void race(int failure_first){struct context held={0},first={0};sthread_t* thread;void* expected;int before;
 held.key=failure_first?100:101;held.hold=1;held.fail=!failure_first;first.fail=failure_first;
 before=retro_atomic_load_acquire_int(&disposed);thread=sthread_create(one,&held);CHECK(thread);await(&held.entered);
 CHECK(!xe_ready_find(&cache,held.key));expected=xe_ready_owned_get(&cache,held.key,create,dispose,&first);CHECK((expected==NULL)==failure_first);
 retro_atomic_store_release_int(&held.release,1);retro_eventcount_notify(&gate);sthread_join(thread);
 CHECK(held.result==expected);CHECK(retro_atomic_load_acquire_int(&disposed)==before+failure_first);
 first.fail=!first.fail;CHECK(xe_ready_owned_get(&cache,held.key,create,dispose,&first)==expected);
}
static void worker(void* opaque){struct context* c=(struct context*)opaque;unsigned i;
 for(i=0;i<4096;++i){uint64_t key=32+(i%64);struct resource* r=(struct resource*)xe_ready_owned_get(&cache,key,create,dispose,c);
  CHECK((r==NULL)==(key%7==0));if(r)CHECK(r->key==key && r->seal==(key^UINT64_C(0x12345678fedcba98)));}}
int main(void){struct context context={0},workers[8]={{0}};sthread_t* threads[8];unsigned i;void* p;int before;
 CHECK(retro_eventcount_init(&gate));
 context.fail=1;CHECK(!xe_ready_owned_get(&cache,UINT64_MAX,create,dispose,&context));before=retro_atomic_load_acquire_int(&emits);
 context.fail=0;CHECK(!xe_ready_owned_get(&cache,UINT64_MAX,create,dispose,&context));CHECK(retro_atomic_load_acquire_int(&emits)==before);
 retro_atomic_store_release_int(&denied,1);CHECK(!xe_ready_owned_get(&cache,0,create,dispose,&context));CHECK(retro_atomic_load_acquire_int(&emits)==before);
 retro_atomic_store_release_int(&denied,0);p=xe_ready_owned_get(&cache,0,create,dispose,&context);CHECK(p && p==context.made);
 context.fail=1;CHECK(xe_ready_owned_get(&cache,0,create,dispose,&context)==p);
 CHECK(!xe_ready_owned_get(NULL,0,create,dispose,&context));CHECK(!xe_ready_owned_get(&cache,0,NULL,dispose,&context));
 race(0);race(1);
 for(i=0;i<8;++i){threads[i]=sthread_create(worker,workers+i);CHECK(threads[i]);}for(i=0;i<8;++i)sthread_join(threads[i]);
 xe_ready_owned_clear(&cache,dispose);xe_ready_owned_clear(&cache,dispose);
 CHECK(retro_atomic_load_acquire_int(&nodes)==retro_atomic_load_acquire_int(&freed_nodes));
 CHECK(retro_atomic_load_acquire_int(&resources)==retro_atomic_load_acquire_int(&disposed));
 context.fail=0;CHECK(xe_ready_owned_get(&cache,0,create,dispose,&context));xe_ready_owned_clear(&cache,dispose);
 CHECK(retro_atomic_load_acquire_int(&nodes)==retro_atomic_load_acquire_int(&freed_nodes));
 CHECK(retro_atomic_load_acquire_int(&resources)==retro_atomic_load_acquire_int(&disposed));retro_eventcount_free(&gate);
 puts("PASS: borrowed payload identity, cached failure, allocation failure before emission, success/failure winner races, 32768 concurrent queries and exact joined disposal/reset");return 0;
}
