/* One bounded retired block, not a linked free list. BSD license. MSVC C89.
 * Atomic exchange transfers exclusive ownership without ABA/hazard pointers.
 * No payload copy, lock, loop, release logging or clock is added here. */
#include "core_shader_ucode_reuse.h"
#include <stdlib.h>
#include <string.h>
#include <retro_atomic.h>
#if !defined(RETRO_ATOMIC_HAS_PTR)
#error Shader snapshot reuse requires atomic pointer exchange
#endif
#if defined(__SANITIZE_ADDRESS__)
#define XE_UCODE_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define XE_UCODE_ASAN 1
#endif
#endif
#ifdef XE_UCODE_ASAN
#include <sanitizer/asan_interface.h>
#define POISON(p,n) __asan_poison_memory_region((p),(n))
#define UNPOISON(p,n) __asan_unpoison_memory_region((p),(n))
#else
#define POISON(p,n) ((void)0)
#define UNPOISON(p,n) ((void)0)
#endif
#ifndef XE_UCODE_REUSE_ALLOC
#define XE_UCODE_REUSE_ALLOC malloc
#endif
#ifndef XE_UCODE_REUSE_FREE
#define XE_UCODE_REUSE_FREE free
#endif
#define MIN_BYTES ((size_t)4096)
#define MAX_BYTES ((size_t)262144)
#define OVERHEAD (sizeof(size_t)+sizeof(void*)+31)
typedef struct reuse_header {size_t capacity;} reuse_header;
static retro_atomic_ptr_t retired;
static uint32_t *payload(void *allocation)
{
 return (uint32_t*)(((uintptr_t)allocation+sizeof(size_t)+sizeof(void*)+31)&~(uintptr_t)31);
}
static void release(void *allocation)
{
 if(allocation){
  UNPOISON(payload(allocation),((reuse_header*)allocation)->capacity);
  XE_UCODE_REUSE_FREE(allocation);
 }
}
int xe_shader_ucode_reuse_allocate(xe_shader_ucode_reuse *owner,size_t count)
{
 size_t bytes,capacity;void *allocation;uint32_t *words;
 if(!owner || owner->words || owner->count || count>(size_t)-1/sizeof(uint32_t))return 0;
 if(!count)return 1;
 bytes=count*sizeof(uint32_t);
 if(bytes<MIN_BYTES){
  allocation=XE_UCODE_REUSE_ALLOC(bytes);
  if(!allocation)return 0;
  owner->words=(uint32_t*)allocation;owner->count=count;return 1;
 }
 if(bytes>(size_t)-1-OVERHEAD)return 0;
 capacity=bytes<=MAX_BYTES?(bytes+31)&~(size_t)31:bytes;
 allocation=NULL;
 if(bytes<=MAX_BYTES){
  allocation=retro_atomic_exchange_ptr(&retired,NULL);
  if(allocation && ((reuse_header*)allocation)->capacity!=capacity){
   release(allocation);allocation=NULL;
  }
 }
 if(!allocation){
  allocation=XE_UCODE_REUSE_ALLOC(capacity+OVERHEAD);
  if(!allocation)return 0;
  ((reuse_header*)allocation)->capacity=capacity;
 }
 words=payload(allocation);UNPOISON(words,capacity);
 memcpy((unsigned char*)words-sizeof(void*),&allocation,sizeof(void*));
 owner->words=words;owner->count=count;return 1;
}
void xe_shader_ucode_reuse_close(xe_shader_ucode_reuse *owner)
{
 void *allocation,*previous;
 if(!owner || !owner->words)return;
 if(owner->count<MIN_BYTES/sizeof(uint32_t)){
  XE_UCODE_REUSE_FREE(owner->words);
 }else{
  memcpy(&allocation,(unsigned char*)owner->words-sizeof(void*),sizeof(void*));
  if(((reuse_header*)allocation)->capacity<=MAX_BYTES){
   POISON(owner->words,((reuse_header*)allocation)->capacity);
   previous=retro_atomic_exchange_ptr(&retired,allocation);release(previous);
  }else release(allocation);
 }
 owner->words=NULL;owner->count=0;
}
void xe_shader_ucode_reuse_clear(void)
{
 release(retro_atomic_exchange_ptr(&retired,NULL));
}
