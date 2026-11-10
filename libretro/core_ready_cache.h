/* Immutable intrusive interning. MSVC C89; BSD license.
 * Zero-initialize the table before first use.
 * Entries are caller-owned and never move/unlink before all users join.
 * Only fully constructed values are published. No pending entry or wait. */
#ifndef XE_CORE_READY_CACHE_H
#define XE_CORE_READY_CACHE_H
#include <stdint.h>
#include <stddef.h>
#include <retro_atomic.h>
#include <retro_inline.h>
#define XE_READY_CACHE_BUCKETS 256
/* Optional deterministic race injection in component tests only. */
#ifndef XE_READY_BEFORE_CAS
#define XE_READY_BEFORE_CAS(head) ((void)0)
#endif

typedef struct xe_ready_node {
   uint64_t key;
   struct xe_ready_node *next;
   void *value;
} xe_ready_node;
typedef struct xe_ready_cache {
   retro_atomic_ptr_t buckets[XE_READY_CACHE_BUCKETS];
} xe_ready_cache;
static INLINE size_t xe_ready_bucket(uint64_t key)
{
   uint32_t hash=(uint32_t)key ^ ((uint32_t)(key>>32)*UINT32_C(0x9e3779b9));
   hash^=hash>>16;hash*=UINT32_C(0x85ebca6b);hash^=hash>>13;
   return hash&(XE_READY_CACHE_BUCKETS-1);
}
static INLINE xe_ready_node *xe_ready_find(const xe_ready_cache *cache,uint64_t key)
{
   xe_ready_node *node=(xe_ready_node*)retro_atomic_load_acquire_ptr(
         &cache->buckets[xe_ready_bucket(key)]);
   while(node){if(node->key==key)return node;node=node->next;}
   return NULL;
}
static INLINE void xe_ready_node_init(xe_ready_node *node,uint64_t key,void *value)
{
   node->key=key;node->next=NULL;node->value=value;
}
/* Returns the one published owner. A losing caller retains its node/value and
 * must destroy them. Append-only lists let CAS losers scan only new prefixes. */
static INLINE xe_ready_node *xe_ready_publish(xe_ready_cache *cache,xe_ready_node *node)
{
   retro_atomic_ptr_t *bucket=&cache->buckets[xe_ready_bucket(node->key)];
   xe_ready_node *head,*scan,*seen=NULL;
   for(;;){
      head=(xe_ready_node*)retro_atomic_load_acquire_ptr(bucket);
      for(scan=head;scan!=seen;scan=scan->next)
         if(scan->key==node->key)return scan;
      node->next=head;
      XE_READY_BEFORE_CAS(head);
      if(retro_atomic_cas_ptr(bucket,head,node))return node;
      seen=head;
   }
}
/* Joined CPU publishers/readers and completed native users only. The caller
 * consumes each immutable chain, saving next before destroying the node. */
static INLINE xe_ready_node *xe_ready_detach(xe_ready_cache *cache,size_t bucket)
{
   return bucket<XE_READY_CACHE_BUCKETS ?
      (xe_ready_node*)retro_atomic_exchange_ptr(&cache->buckets[bucket],NULL):NULL;
}
#endif
