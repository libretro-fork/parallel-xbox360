/* Immutable packed binding publication. MSVC C89; BSD license. */
#ifndef XE_CORE_BINDING_SNAPSHOT_H
#define XE_CORE_BINDING_SNAPSHOT_H
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <retro_atomic.h>
#include <retro_inline.h>
#ifndef XE_BINDING_SNAPSHOT_ALLOC
#define XE_BINDING_SNAPSHOT_ALLOC calloc
#define XE_BINDING_SNAPSHOT_FREE free
#endif
/* One allocation: header followed by zero-initialized four-byte records.
 * Only unpublished owners are mutable. Join every reader before clear. */
typedef struct xe_binding_snapshot {
   size_t texture_count, sampler_count;
   uint32_t used_texture_mask;
} xe_binding_snapshot;
static INLINE void *xe_binding_snapshot_records(xe_binding_snapshot *owner)
{
   return owner ? (void*)(owner+1) : NULL;
}
static INLINE xe_binding_snapshot *xe_binding_snapshot_new(size_t textures,size_t samplers)
{
   xe_binding_snapshot *owner;
   size_t count;
   if(textures>(size_t)-1-samplers)return NULL;
   count=textures+samplers;
   if(count>((size_t)-1-sizeof(*owner))/sizeof(uint32_t))return NULL;
   owner=(xe_binding_snapshot*)XE_BINDING_SNAPSHOT_ALLOC(1,sizeof(*owner)+count*sizeof(uint32_t));
   if(owner){owner->texture_count=textures;owner->sampler_count=samplers;}
   return owner;
}
static INLINE const xe_binding_snapshot *xe_binding_snapshot_get(const retro_atomic_ptr_t *slot)
{
   return slot ? (const xe_binding_snapshot*)retro_atomic_load_acquire_ptr(slot) : NULL;
}
static INLINE int xe_binding_snapshot_equal(const xe_binding_snapshot *a,const xe_binding_snapshot *b)
{
   if(!a || !b || a->texture_count!=b->texture_count || a->sampler_count!=b->sampler_count ||
      a->used_texture_mask!=b->used_texture_mask)return 0;
   return !memcmp(a+1,b+1,(a->texture_count+a->sampler_count)*sizeof(uint32_t));
}
/* Consumes candidate on all paths. Readers borrow one immutable allocation;
 * publication never waits for another producer or copies record payloads. */
static INLINE int xe_binding_snapshot_publish(retro_atomic_ptr_t *slot,xe_binding_snapshot *candidate)
{
   const xe_binding_snapshot *published;
   int equal;
   if(!slot || !candidate){XE_BINDING_SNAPSHOT_FREE(candidate);return 0;}
   if(retro_atomic_cas_ptr(slot,NULL,candidate))return 1;
   published=xe_binding_snapshot_get(slot);
   equal=xe_binding_snapshot_equal(published,candidate);
   XE_BINDING_SNAPSHOT_FREE(candidate);
   return equal;
}
static INLINE void xe_binding_snapshot_clear(retro_atomic_ptr_t *slot)
{
   if(slot)XE_BINDING_SNAPSHOT_FREE(retro_atomic_exchange_ptr(slot,NULL));
}
#endif
