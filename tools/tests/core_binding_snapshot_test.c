/* Immutable binding ownership and publication. MSVC C89. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <retro_atomic.h>
#include <rthreads/rthreads.h>
#include <rthreads/retro_eventcount.h>
static retro_atomic_int_t allocations,frees,denied;
static void* allocate(size_t count,size_t bytes){void* p;
 if(retro_atomic_load_acquire_int(&denied))return NULL;
 p=calloc(count,bytes);if(p)retro_atomic_fetch_add_int(&allocations,1);return p;}
static void dispose(void* p){if(p)retro_atomic_fetch_add_int(&frees,1);free(p);}
#define XE_BINDING_SNAPSHOT_ALLOC allocate
#define XE_BINDING_SNAPSHOT_FREE dispose
#include "libretro/core_binding_snapshot.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"snapshot line %u\n",(unsigned)__LINE__);exit(1);}}while(0)
static retro_atomic_ptr_t slot;
static retro_eventcount_t gate;
static retro_atomic_int_t entered,released;
static xe_binding_snapshot* make(void){xe_binding_snapshot* p;uint32_t* words;unsigned i;
 p=xe_binding_snapshot_new(2,3);CHECK(p);words=(uint32_t*)xe_binding_snapshot_records(p);
 for(i=0;i<5;++i){CHECK(!words[i]);words[i]=i+42;}p->used_texture_mask=3;return p;}
static void await(retro_atomic_int_t* flag){int key;
 while(!retro_atomic_load_acquire_int(flag)){key=retro_eventcount_prepare_wait(&gate);
 if(retro_atomic_load_acquire_int(flag))retro_eventcount_cancel_wait(&gate);
 else CHECK(retro_eventcount_commit_wait_timeout(&gate,key,5000000) || retro_atomic_load_acquire_int(flag));}}
static void held(void* opaque){xe_binding_snapshot* p=make();(void)opaque;
 retro_atomic_store_release_int(&entered,1);retro_eventcount_notify(&gate);await(&released);
 CHECK(xe_binding_snapshot_publish(&slot,p));}
static void worker(void* opaque){unsigned repeat,i;const xe_binding_snapshot* p;const uint32_t* words;(void)opaque;
 for(repeat=0;repeat<4096;++repeat){CHECK(xe_binding_snapshot_publish(&slot,make()));
 p=xe_binding_snapshot_get(&slot);CHECK(p && p->texture_count==2 && p->sampler_count==3 && p->used_texture_mask==3);
 words=(const uint32_t*)(p+1);for(i=0;i<5;++i)CHECK(words[i]==i+42);}}
int main(void){xe_binding_snapshot *p,*first;const xe_binding_snapshot* ready;sthread_t* threads[8];unsigned i;int before;
 CHECK(retro_eventcount_init(&gate));CHECK(!xe_binding_snapshot_get(NULL));CHECK(!xe_binding_snapshot_get(&slot));
 CHECK(!xe_binding_snapshot_new((size_t)-1,1));CHECK(!xe_binding_snapshot_new((size_t)-1/4,0));
 retro_atomic_store_release_int(&denied,1);CHECK(!xe_binding_snapshot_new(1,1));retro_atomic_store_release_int(&denied,0);
 CHECK(!xe_binding_snapshot_publish(&slot,NULL));CHECK(!xe_binding_snapshot_publish(NULL,make()));
 p=xe_binding_snapshot_new(0,0);CHECK(p);CHECK(xe_binding_snapshot_publish(&slot,p));CHECK(xe_binding_snapshot_get(&slot)==p);
 xe_binding_snapshot_clear(&slot);CHECK(!xe_binding_snapshot_get(&slot));
 threads[0]=sthread_create(held,NULL);CHECK(threads[0]);await(&entered);CHECK(!xe_binding_snapshot_get(&slot));
 first=make();CHECK(xe_binding_snapshot_publish(&slot,first));CHECK(xe_binding_snapshot_get(&slot)==first);
 retro_atomic_store_release_int(&released,1);retro_eventcount_notify(&gate);sthread_join(threads[0]);CHECK(xe_binding_snapshot_get(&slot)==first);
 before=retro_atomic_load_acquire_int(&frees);p=make();p->used_texture_mask^=1;CHECK(!xe_binding_snapshot_publish(&slot,p));
 p=make();((uint32_t*)(p+1))[0]^=1;CHECK(!xe_binding_snapshot_publish(&slot,p));
 p=xe_binding_snapshot_new(1,4);CHECK(p);CHECK(!xe_binding_snapshot_publish(&slot,p));CHECK(retro_atomic_load_acquire_int(&frees)==before+3);
 for(i=0;i<8;++i){threads[i]=sthread_create(worker,NULL);CHECK(threads[i]);}for(i=0;i<8;++i)sthread_join(threads[i]);
 ready=xe_binding_snapshot_get(&slot);CHECK(ready==first);xe_binding_snapshot_clear(&slot);xe_binding_snapshot_clear(&slot);xe_binding_snapshot_clear(NULL);
 CHECK(retro_atomic_load_acquire_int(&allocations)==retro_atomic_load_acquire_int(&frees));retro_eventcount_free(&gate);
 puts("PASS: held publisher, immutable borrowed identity, 32768 racing publications, mismatch/overflow/allocation-failure rejection and exact joined disposal");return 0;}
