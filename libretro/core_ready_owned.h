/* Owned immutable resource cache. MSVC C89; BSD license.
 * Zero initialize. Callbacks must not throw or reenter/mutate this cache. A NULL result
 * is a completed cached failure; it is distinct from a missing entry. */
#ifndef XE_CORE_READY_OWNED_H
#define XE_CORE_READY_OWNED_H
#include "core_ready_cache.h"
#include <stdlib.h>
#ifndef XE_READY_OWNED_ALLOC
#define XE_READY_OWNED_ALLOC malloc
#define XE_READY_OWNED_FREE free
#endif

typedef void *(*xe_ready_create)(void *opaque,uint64_t key);
typedef void (*xe_ready_dispose)(void *resource);
/* No payload copy. Published resources are borrowed until joined clear. */
static INLINE void *xe_ready_owned_get(xe_ready_cache *cache,uint64_t key,
      xe_ready_create create,xe_ready_dispose dispose,void *opaque)
{
   xe_ready_node *node,*winner;
   void *resource;
   if(!cache || !create || !dispose)return NULL;
   node=xe_ready_find(cache,key);
   if(node)return node->value;
   node=(xe_ready_node*)XE_READY_OWNED_ALLOC(sizeof(*node));
   if(!node)return NULL;
   resource=create(opaque,key);
   xe_ready_node_init(node,key,resource);
   winner=xe_ready_publish(cache,node);
   if(winner!=node){
      if(resource)dispose(resource);
      XE_READY_OWNED_FREE(node);
   }
   return winner->value;
}
/* Join all CPU creators/borrowers and retire their native consumers first.
 * Clears successful resources, cached failures and metadata exactly once. */
static INLINE void xe_ready_owned_clear(xe_ready_cache *cache,xe_ready_dispose dispose)
{
   size_t bucket;
   xe_ready_node *node,*next;
   if(!cache || !dispose)return;
   for(bucket=0;bucket<XE_READY_CACHE_BUCKETS;++bucket){
      node=xe_ready_detach(cache,bucket);
      while(node){
         next=node->next;
         if(node->value)dispose(node->value);
         XE_READY_OWNED_FREE(node);
         node=next;
      }
   }
}
#endif
