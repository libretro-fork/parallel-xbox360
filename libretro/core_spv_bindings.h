/* Single-owner translation binding scratch. Keys are logical fields, never
 * persistent-format bytes. Clear retains storage; views expire on growth. */
#ifndef XE_CORE_SPV_BINDINGS_H
#define XE_CORE_SPV_BINDINGS_H
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <retro_inline.h>
#ifndef XE_SPV_BINDINGS_ALLOC
#define XE_SPV_BINDINGS_ALLOC malloc
#define XE_SPV_BINDINGS_FREE free
#endif
enum { XE_SPV_BINDINGS_INLINE = 16 };
typedef struct xe_spv_binding_record { uint32_t key,variable; } xe_spv_binding_record;
typedef struct xe_spv_bindings {
   xe_spv_binding_record *heap;
   size_t count,capacity;
   xe_spv_binding_record local[XE_SPV_BINDINGS_INLINE];
} xe_spv_bindings;
static INLINE xe_spv_binding_record *xe_spv_bindings_data(xe_spv_bindings *s)
{
   return s->heap?s->heap:s->local;
}
static INLINE const xe_spv_binding_record *xe_spv_bindings_view(const xe_spv_bindings *s)
{
   return s->heap?s->heap:s->local;
}
static INLINE int xe_spv_bindings_reserve(xe_spv_bindings *s,size_t count)
{
   size_t capacity=s->heap?s->capacity:XE_SPV_BINDINGS_INLINE;
   xe_spv_binding_record *next;
   if(count<=capacity)return 1;
   if(count>(size_t)-1/sizeof(*next))return 0;
   if(capacity<=(size_t)-1/(2*sizeof(*next)) && count<capacity*2)count=capacity*2;
   next=(xe_spv_binding_record*)XE_SPV_BINDINGS_ALLOC(count*sizeof(*next));
   if(!next)return 0;
   if(s->count)memcpy(next,xe_spv_bindings_view(s),s->count*sizeof(*next));
   XE_SPV_BINDINGS_FREE(s->heap);s->heap=next;s->capacity=count;
   return 1;
}
static INLINE xe_spv_binding_record *xe_spv_bindings_append(xe_spv_bindings *s,uint32_t key)
{
   xe_spv_binding_record *record;
   if(s->count==(size_t)-1 || !xe_spv_bindings_reserve(s,s->count+1))return NULL;
   record=xe_spv_bindings_data(s)+s->count++;
   record->key=key;record->variable=0;
   return record;
}
static INLINE size_t xe_spv_bindings_find(const xe_spv_bindings *s,uint32_t key)
{
   const xe_spv_binding_record *records=xe_spv_bindings_view(s);
   size_t i;
   for(i=0;i<s->count;++i)if(records[i].key==key)return i;
   return (size_t)-1;
}
static INLINE void xe_spv_bindings_close(xe_spv_bindings *s)
{
   XE_SPV_BINDINGS_FREE(s->heap);s->heap=NULL;s->count=s->capacity=0;
}
/* Inputs are the guest's five-bit fetch index and encoded enum fields.
 * Invalid values fail instead of truncating distinct keys into aliases. */
static INLINE int xe_spv_texture_binding_key(uint32_t *key,uint32_t fetch,
      uint32_t dimension,uint32_t is_signed)
{
   if(!key || fetch>=32 || dimension>=4 || is_signed>=2)return 0;
   *key=fetch|(dimension<<5)|(is_signed<<7);return 1;
}
static INLINE int xe_spv_sampler_binding_key(uint32_t *key,uint32_t fetch,
      uint32_t mag,uint32_t min,uint32_t mip,uint32_t aniso)
{
   if(!key || fetch>=32 || mag>=4 || min>=4 || mip>=4 || aniso>=8)return 0;
   *key=fetch|(mag<<5)|(min<<7)|(mip<<9)|(aniso<<11);return 1;
}
#endif
