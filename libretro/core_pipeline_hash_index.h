/* Single-owner intrusive hash ranges. BSD license; MSVC C89.
 * Caller provides a fixed, final bucket array during initialization and owns
 * every zero-initialized node. Neither insertion nor lookup allocates, copies
 * payloads, grows buckets or changes an owner's address. Hash equality is NOT
 * full-key equality: callers must compare every candidate's actual key.
 * Serialize index mutation; workers may borrow owners, not traverse this index.
 * Drain only after all CPU/GPU consumers finish. No concurrent reclamation. */
#ifndef XE_CORE_PIPELINE_HASH_INDEX_H
#define XE_CORE_PIPELINE_HASH_INDEX_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <retro_inline.h>
struct xe_pipeline_hash_index;
typedef struct xe_pipeline_hash_node {
 struct xe_pipeline_hash_node *next;
 struct xe_pipeline_hash_index *index;
 uint64_t hash;
} xe_pipeline_hash_node;
typedef struct xe_pipeline_hash_index {
 xe_pipeline_hash_node **buckets;
 size_t mask,count;
} xe_pipeline_hash_index;
static INLINE size_t xe_pipeline_hash_bucket(const xe_pipeline_hash_index *index,uint64_t hash)
{
 /* The caller supplies the same fully mixed hash used by the old identity
  * hasher. Preserve its bucket bits rather than remapping a good distribution. */
 return (size_t)hash&index->mask;
}
/* Zero-initialize the index before first init. Reject reinitializing live
 * owners; failure preserves the existing index and the supplied storage. */
static INLINE int xe_pipeline_hash_init(xe_pipeline_hash_index *index,
 xe_pipeline_hash_node **buckets,size_t count)
{
 if(!index || index->count || !buckets || !count || (count&(count-1)) ||
  count>(size_t)-1/sizeof(*buckets))return 0;
 memset(buckets,0,count*sizeof(*buckets));index->buckets=buckets;
 index->mask=count-1;index->count=0;return 1;
}
static INLINE xe_pipeline_hash_node *xe_pipeline_hash_first(const xe_pipeline_hash_index *index,uint64_t hash)
{
 xe_pipeline_hash_node *node;size_t primary,secondary;
 if(!index || !index->buckets || !index->count)return NULL;
 primary=xe_pipeline_hash_bucket(index,hash);node=index->buckets[primary];
 while(node && node->hash!=hash)node=node->next;
 if(node)return node;
 secondary=(size_t)(hash^(hash>>32))&index->mask;
 if(secondary==primary)return NULL;
 node=index->buckets[secondary];
 while(node && node->hash!=hash)node=node->next;
 return node;
}
static INLINE xe_pipeline_hash_node *xe_pipeline_hash_next(const xe_pipeline_hash_node *current)
{
 xe_pipeline_hash_node *node;
 if(!current || !current->index)return NULL;
 node=current->next;
 while(node && node->hash!=current->hash)node=node->next;
 return node;
}
/* Equal hashes and distinct full keys intentionally coexist. Already-linked
 * owners are rejected, including insertion into a different index. */
static INLINE int xe_pipeline_hash_insert(xe_pipeline_hash_index *index,
 xe_pipeline_hash_node *node,uint64_t hash)
{
 size_t bucket,secondary;xe_pipeline_hash_node *same;
 if(!index || !index->buckets || !node || node->index || index->count==(size_t)-1)return 0;
 same=xe_pipeline_hash_first(index,hash);
 node->hash=hash;node->index=index;
 if(same){
  /* Keep every equal full hash in one chain, so next() never needs a second
   * cursor or copied lookup state. Owners and the original anchor stay put. */
  node->next=same->next;same->next=node;
 }else{
  bucket=xe_pipeline_hash_bucket(index,hash);
  secondary=(size_t)(hash^(hash>>32))&index->mask;
  /* A second empty choice prevents a low-bit cluster from turning every
   * failed hash check into a walk through widely spaced pipeline owners. */
  if(index->buckets[bucket] && !index->buckets[secondary])bucket=secondary;
  node->next=index->buckets[bucket];index->buckets[bucket]=node;
 }
 ++index->count;return 1;
}
/* Initialize cursor to zero for a complete detached drain. Each returned node
 * is unpublished and reusable. No payload move or deletion occurs here. */
static INLINE xe_pipeline_hash_node *xe_pipeline_hash_drain(xe_pipeline_hash_index *index,size_t *cursor)
{
 xe_pipeline_hash_node *node;
 if(!index || !cursor || !index->buckets || !index->count)return NULL;
 while(*cursor<=index->mask){
  node=index->buckets[*cursor];
  if(node){index->buckets[*cursor]=node->next;node->next=NULL;node->index=NULL;--index->count;return node;}
  ++*cursor;
 }
 return NULL;
}
#endif
