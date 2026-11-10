/* Ready immutable owners and exclusive Vulkan layout inputs. MSVC C89. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <retro_atomic.h>
#include <rthreads/rthreads.h>
#include <rthreads/retro_eventcount.h>
static void before_cas(void);
#define XE_READY_BEFORE_CAS(head) before_cas()
#include "libretro/core_ready_cache.h"
static int deny_alloc;static unsigned allocs;
static void *scratch_alloc(size_t bytes){++allocs;return deny_alloc?NULL:malloc(bytes);}
#define XE_DESCRIPTOR_ALLOC scratch_alloc
#define XE_DESCRIPTOR_FREE free
#include "libretro/core_vk_layout_input.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"layout ready line %u\n",(unsigned)__LINE__);exit(1);}}while(0)
#define THREADS 8
#define KEYS 128
struct entry {xe_ready_node node;uint64_t guard;unsigned owner;};
static xe_ready_cache table;
static uint64_t keys[KEYS];
static retro_atomic_int_t started,created,freed;
static retro_eventcount_t gate;
static retro_atomic_int_t staged_mode,staged_calls,staged_release;
static xe_ready_cache staged_table;
static struct entry staged_entries[2];
static void before_cas(void){int key;
 if(!retro_atomic_load_acquire_int(&staged_mode))return;
 if(retro_atomic_fetch_add_int(&staged_calls,1)>=2)return;
 retro_eventcount_notify(&gate);
 while(!retro_atomic_load_acquire_int(&staged_release)){
  key=retro_eventcount_prepare_wait(&gate);
  if(retro_atomic_load_acquire_int(&staged_release))retro_eventcount_cancel_wait(&gate);
  else CHECK(retro_eventcount_commit_wait_timeout(&gate,key,5000000) || (retro_atomic_load_acquire_int(&staged_release)));
 }
}
static void staged_publish(void* opaque){struct entry* entry=(struct entry*)opaque;CHECK(xe_ready_publish(&staged_table,&entry->node)==&entry->node);}
static void check_cas_retry(void){sthread_t* threads[2];unsigned i;int key;
 retro_atomic_store_release_int(&staged_mode,1);
 for(i=0;i<2;++i){xe_ready_node_init(&staged_entries[i].node,keys[i],staged_entries+i);threads[i]=sthread_create(staged_publish,staged_entries+i);CHECK(threads[i]);}
 while(retro_atomic_load_acquire_int(&staged_calls)!=2){
  key=retro_eventcount_prepare_wait(&gate);
  if(retro_atomic_load_acquire_int(&staged_calls)==2)retro_eventcount_cancel_wait(&gate);
  else CHECK(retro_eventcount_commit_wait_timeout(&gate,key,5000000) || (retro_atomic_load_acquire_int(&staged_calls)==2));
 }
 retro_atomic_store_release_int(&staged_release,1);retro_eventcount_notify(&gate);
 for(i=0;i<2;++i)sthread_join(threads[i]);
 CHECK(retro_atomic_load_acquire_int(&staged_calls)==3);
 for(i=0;i<2;++i)CHECK(xe_ready_find(&staged_table,keys[i])==&staged_entries[i].node);
 CHECK(xe_ready_detach(&staged_table,0));retro_atomic_store_release_int(&staged_mode,0);
}
static void wait_start(void){int key;
 while(!retro_atomic_load_acquire_int(&started)){
  key=retro_eventcount_prepare_wait(&gate);
  if(retro_atomic_load_acquire_int(&started))retro_eventcount_cancel_wait(&gate);
  else CHECK(retro_eventcount_commit_wait_timeout(&gate,key,5000000) || (retro_atomic_load_acquire_int(&started)));
 }
}
static void worker(void* opaque){unsigned who=(unsigned)(uintptr_t)opaque,i;xe_ready_node *node;struct entry* entry;
 wait_start();
 for(i=0;i<4096;++i){uint64_t key=keys[(i*17+who)%KEYS];
  entry=(struct entry*)malloc(sizeof(*entry));CHECK(entry);retro_atomic_fetch_add_int(&created,1);
  entry->guard=key^UINT64_C(0x87abcddcba129876);entry->owner=who;
  xe_ready_node_init(&entry->node,key,entry);node=xe_ready_publish(&table,&entry->node);
  if(node!=&entry->node){memset(entry,0xa5,sizeof(*entry));free(entry);retro_atomic_fetch_add_int(&freed,1);}
  node=xe_ready_find(&table,key);CHECK(node && node->key==key);
  entry=(struct entry*)node->value;CHECK(entry->guard==(key^UINT64_C(0x87abcddcba129876)) && entry->owner<THREADS);
 }
}
static retro_atomic_int_t held,release_held;
static void held_creator(void* opaque){struct entry* entry=(struct entry*)opaque;int key;
 retro_atomic_store_release_int(&held,1);retro_eventcount_notify(&gate);
 while(!retro_atomic_load_acquire_int(&release_held)){
  key=retro_eventcount_prepare_wait(&gate);
  if(retro_atomic_load_acquire_int(&release_held))retro_eventcount_cancel_wait(&gate);
  else CHECK(retro_eventcount_commit_wait_timeout(&gate,key,5000000) || (retro_atomic_load_acquire_int(&release_held)));
 }
 CHECK(xe_ready_publish(&table,&entry->node)==&entry->node);
}
static void check_held_creator(void){struct entry* entry=(struct entry*)malloc(sizeof(*entry));sthread_t* thread;unsigned i;int key;
 CHECK(entry);entry->guard=UINT64_MAX^UINT64_C(0x87abcddcba129876);entry->owner=0;
 xe_ready_node_init(&entry->node,UINT64_MAX,entry);thread=sthread_create(held_creator,entry);CHECK(thread);
 while(!retro_atomic_load_acquire_int(&held)){
  key=retro_eventcount_prepare_wait(&gate);
  if(retro_atomic_load_acquire_int(&held))retro_eventcount_cancel_wait(&gate);
  else CHECK(retro_eventcount_commit_wait_timeout(&gate,key,5000000) || (retro_atomic_load_acquire_int(&held)));
 }
 CHECK(!xe_ready_find(&table,UINT64_MAX));
 for(i=0;i<100000;++i)CHECK(xe_ready_find(&table,keys[i%KEYS]));
 retro_atomic_store_release_int(&release_held,1);retro_eventcount_notify(&gate);sthread_join(thread);
 CHECK(xe_ready_find(&table,UINT64_MAX)==&entry->node);
}
static void inputs(void){xe_descriptor_scratch scratch={0};VkDescriptorSetLayoutBinding *input,*old;size_t i;unsigned calls;uint64_t key=99,other;
 CHECK(!xe_vk_texture_layout_key((size_t)-1,0,0,&key) && key==99);
 CHECK(!xe_vk_texture_layout_key(0,32768,0,&key) && key==99);
 CHECK(!xe_vk_pipeline_layout_key(0,0,65536,0,&key) && key==99);
 CHECK(!xe_vk_pipeline_layout_key(0,32768,0,0,&key) && key==99);
 CHECK(xe_vk_texture_layout_key(65535,32767,1,&key) && key==UINT32_MAX);
 CHECK(xe_vk_pipeline_layout_key(65535,32767,65535,32767,&key));
 CHECK(key==UINT64_C(0x7fffffff7fffffff));
 CHECK(xe_vk_pipeline_layout_key(0,1,0,0,&key));CHECK(xe_vk_pipeline_layout_key(0,0,1,0,&other));CHECK(key!=other);
 for(i=1;i<=256;i*=2){
  input=xe_vk_layout_bindings(&scratch,i,i/2,VK_SHADER_STAGE_VERTEX_BIT);CHECK(input);
  memset(scratch.data,0xa5,scratch.capacity);
  input=xe_vk_layout_bindings(&scratch,i,i/2,VK_SHADER_STAGE_FRAGMENT_BIT);CHECK(input);
  {size_t j;for(j=0;j<i+i/2;++j){CHECK(input[j].binding==j && input[j].descriptorCount==1 &&
    input[j].stageFlags==VK_SHADER_STAGE_FRAGMENT_BIT && !input[j].pImmutableSamplers &&
    input[j].descriptorType==(j<i?VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:VK_DESCRIPTOR_TYPE_SAMPLER));}}
 }
 old=input;calls=allocs;for(i=0;i<100000;++i)CHECK(xe_vk_layout_bindings(&scratch,32,16,VK_SHADER_STAGE_VERTEX_BIT)==old);
 CHECK(allocs==calls);deny_alloc=1;CHECK(!xe_vk_layout_bindings(&scratch,2048,2048,VK_SHADER_STAGE_VERTEX_BIT));
 CHECK(scratch.data==(uint8_t*)old);CHECK(!xe_vk_layout_bindings(&scratch,0,32768,VK_SHADER_STAGE_VERTEX_BIT));
 CHECK(xe_vk_layout_bindings(&scratch,32,16,VK_SHADER_STAGE_VERTEX_BIT)==old);deny_alloc=0;
 xe_descriptor_scratch_free(&scratch);CHECK(!scratch.data && !scratch.capacity);
}
int main(void){sthread_t* threads[THREADS];unsigned i,count=0;uint64_t value=0;size_t bucket;xe_ready_node *node,*next;
 CHECK(retro_eventcount_init(&gate));
 /* Force a full collision chain, including the valid zero key. */
 while(count<KEYS){if(xe_ready_bucket(value)==0)keys[count++]=value;++value;}
 CHECK(!xe_ready_find(&table,0));check_cas_retry();inputs();
 for(i=0;i<THREADS;++i){threads[i]=sthread_create(worker,(void*)(uintptr_t)i);CHECK(threads[i]);}
 retro_atomic_store_release_int(&started,1);retro_eventcount_notify(&gate);
 for(i=0;i<THREADS;++i)sthread_join(threads[i]);
 CHECK(retro_atomic_load_acquire_int(&created)==THREADS*4096);
 CHECK(retro_atomic_load_acquire_int(&freed)==THREADS*4096-KEYS);
 check_held_creator();
 count=0;for(bucket=0;bucket<XE_READY_CACHE_BUCKETS;++bucket){node=xe_ready_detach(&table,bucket);
  while(node){next=node->next;CHECK(node->value && ((struct entry*)node->value)->guard==(node->key^UINT64_C(0x87abcddcba129876)));
   free(node->value);++count;node=next;}}
 CHECK(count==KEYS+1 && !xe_ready_find(&table,0));
 CHECK(!xe_ready_detach(&table,XE_READY_CACHE_BUCKETS));
 {struct entry entry;xe_ready_node_init(&entry.node,UINT64_MAX,&entry);CHECK(xe_ready_publish(&table,&entry.node)==&entry.node);
 CHECK(xe_ready_find(&table,UINT64_MAX)==&entry.node);CHECK(xe_ready_detach(&table,xe_ready_bucket(UINT64_MAX))==&entry.node);}
 retro_eventcount_free(&gate);
 puts("PASS: 32768 raced owners/128 colliding keys, immediate losing-owner frees, forced CAS-prefix retry, held-creator independent hits and exact joined reset; bounded keys, poisoned bindings, growth failure and 100000 allocation-free scratch reuses");return 0;
}
