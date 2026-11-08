/* Four-entry guest shader return stack lowering. MSVC C89, no host storage
 * allocation. Values are backend SSA IDs, not CPU addresses. Callers branch on
 * can_push/can_pop before emitting the corresponding mutation. */
#ifndef XE_CORE_SHADER_CALL_H
#define XE_CORE_SHADER_CALL_H
#include <stdint.h>
#include <retro_inline.h>
enum { XE_CALL_ADD, XE_CALL_SUB, XE_CALL_LESS, XE_CALL_NOT_EQUAL };
typedef struct xe_shader_call_ops {
   void *context;
   uint32_t (*constant)(void *, uint32_t);
   uint32_t (*load)(void *, uint32_t);
   int (*store)(void *, uint32_t, uint32_t);
   uint32_t (*binary)(void *, unsigned, uint32_t, uint32_t);
   uint32_t (*extract)(void *, uint32_t, unsigned);
   uint32_t (*vector4)(void *, const uint32_t *);
} xe_shader_call_ops;
static INLINE uint32_t xe_shader_call_guard(const xe_shader_call_ops *o,
      uint32_t depth, int push) {
   return o->binary(o->context,push?XE_CALL_LESS:XE_CALL_NOT_EQUAL,
      o->load(o->context,depth),o->constant(o->context,push?4u:0u));
}
static INLINE int xe_shader_call_push(const xe_shader_call_ops *o,
      uint32_t stack,uint32_t depth,uint32_t next) {
   uint32_t previous,value,lanes[4],i;
   previous=o->load(o->context,stack);if(!previous)return 0;
   lanes[0]=o->constant(o->context,next);
   for(i=1;i<4;++i)lanes[i]=o->extract(o->context,previous,i-1);
   for(i=0;i<4;++i)if(!lanes[i])return 0;
   value=o->vector4(o->context,lanes);if(!value)return 0;
   if(!o->store(o->context,value,stack))return 0;
   value=o->binary(o->context,XE_CALL_ADD,o->load(o->context,depth),
      o->constant(o->context,1));
   return value && o->store(o->context,value,depth);
}
static INLINE uint32_t xe_shader_call_pop(const xe_shader_call_ops *o,
      uint32_t stack,uint32_t depth) {
   uint32_t previous,target,value,lanes[4],i;
   previous=o->load(o->context,stack);if(!previous)return 0;
   target=o->extract(o->context,previous,0);if(!target)return 0;
   for(i=0;i<3;++i)lanes[i]=o->extract(o->context,previous,i+1);
   lanes[3]=o->constant(o->context,0);
   for(i=0;i<4;++i)if(!lanes[i])return 0;
   value=o->vector4(o->context,lanes);if(!value)return 0;
   if(!o->store(o->context,value,stack))return 0;
   value=o->binary(o->context,XE_CALL_SUB,o->load(o->context,depth),
      o->constant(o->context,1));
   if(!value || !o->store(o->context,value,depth))return 0;
   return target;
}
#endif
