/* Backend-neutral integer SSA recipe for exact finite binary32 RTZ+FTZ.
 * MSVC C89. No new buffers/arrays: values belong to the caller's IR arena.
 * Both backend adapters must preserve 32-bit wrap and defined CLZ(0)=32. */
#ifndef XE_CORE_FLOAT_RTZ_EMIT_H
#define XE_CORE_FLOAT_RTZ_EMIT_H
#include <stdint.h>
#include "../third_party/libretro-common/include/retro_inline.h"
typedef uintptr_t xe_rtz_id;
enum xe_rtz_opcode {
   XE_RTZ_CONST,XE_RTZ_ADD,XE_RTZ_SUB,XE_RTZ_MUL,XE_RTZ_AND,XE_RTZ_OR,XE_RTZ_XOR,
   XE_RTZ_SHL,XE_RTZ_SHR,XE_RTZ_EQ,XE_RTZ_ULT,XE_RTZ_ULE,XE_RTZ_UGE,
   XE_RTZ_BOOL_AND,XE_RTZ_SELECT,XE_RTZ_CLZ,XE_RTZ_MUL_TRUNC16
};
typedef struct xe_rtz_emitter {
   void *context;
   xe_rtz_id (*emit)(void *,unsigned,xe_rtz_id,xe_rtz_id,xe_rtz_id,uint32_t);
   int failed;
} xe_rtz_emitter;
static INLINE xe_rtz_id xe_rtz_node(xe_rtz_emitter *e,unsigned op,xe_rtz_id a,xe_rtz_id b,xe_rtz_id c,uint32_t literal) {
   xe_rtz_id result;
   if(!e || e->failed || !e->emit)return 0;
   if(op!=XE_RTZ_CONST && (!a || (op!=XE_RTZ_CLZ && !b) || (op==XE_RTZ_SELECT && !c))) {e->failed=1;return 0;}
   result=e->emit(e->context,op,a,b,c,literal);
   if(!result)e->failed=1;
   return result;
}
#define RC(x) xe_rtz_node(e,XE_RTZ_CONST,0,0,0,(x))
#define RB(op,a,b) xe_rtz_node(e,XE_RTZ_##op,(a),(b),0,0)
#define RS(c,a,b) xe_rtz_node(e,XE_RTZ_SELECT,(c),(a),(b),0)
static INLINE xe_rtz_id xe_rtz_emit_flush(xe_rtz_emitter *e,xe_rtz_id bits) {
   xe_rtz_id exponent=RB(AND,bits,RC(UINT32_C(0x7f800000)));
   return RS(RB(EQ,exponent,RC(0)),RB(AND,bits,RC(UINT32_C(0x80000000))),bits);
}
static INLINE xe_rtz_id xe_rtz_emit_add(xe_rtz_emitter *e,xe_rtz_id a,xe_rtz_id b,xe_rtz_id native_result) {
   xe_rtz_id z=RC(0),one=RC(1),c23=RC(23),mask=RC(UINT32_C(0x7fffffff));
   xe_rtz_id frac=RC(UINT32_C(0x7fffff)),hidden=RC(UINT32_C(0x800000)),signmask=RC(UINT32_C(0x80000000));
   xe_rtz_id aa=RB(AND,a,mask),bb=RB(AND,b,mask),ea=RB(SHR,aa,c23),eb=RB(SHR,bb,c23);
   xe_rtz_id finite=RB(BOOL_AND,RB(ULT,ea,RC(255)),RB(ULT,eb,RC(255)));
   xe_rtz_id swap,magnitude_large,magnitude_small,le,se,la,lb,sign,sa,sb,diff,shift,jam,sum,carry,exponent,result,empty;
   aa=RS(RB(EQ,ea,z),z,aa);bb=RS(RB(EQ,eb,z),z,bb);
   swap=RB(ULT,aa,bb);
   magnitude_large=RS(swap,bb,aa);magnitude_small=RS(swap,aa,bb);le=RS(swap,eb,ea);se=RS(swap,ea,eb);
   la=RS(swap,b,a);lb=RS(swap,a,b);sign=RB(AND,la,signmask);
   sa=RS(RB(EQ,le,z),z,RB(SHL,RB(OR,RB(AND,magnitude_large,frac),hidden),RC(3)));
   sb=RS(RB(EQ,se,z),z,RB(SHL,RB(OR,RB(AND,magnitude_small,frac),hidden),RC(3)));
   diff=RB(SUB,le,se);shift=RS(RB(UGE,diff,RC(31)),RC(31),diff);
   jam=RS(RB(EQ,diff,z),z,RS(RB(EQ,RB(SHL,sb,RB(AND,RB(SUB,RC(32),shift),RC(31))),z),z,one));
   sb=RB(OR,RB(SHR,sb,shift),jam);
   sum=RS(RB(EQ,RB(AND,RB(XOR,la,lb),signmask),z),RB(ADD,sa,sb),RB(SUB,sa,sb));
   empty=RB(EQ,sum,z);carry=RB(SHR,sum,RC(27));
   sum=RS(RB(EQ,carry,z),sum,RB(OR,RB(SHR,sum,one),RB(AND,sum,one)));
   shift=RB(SUB,xe_rtz_node(e,XE_RTZ_CLZ,sum,0,0,0),RC(5));
   exponent=RB(ADD,le,carry);
   result=RB(OR,sign,RB(OR,RB(SHL,RB(SUB,exponent,shift),c23),RB(AND,RB(SHR,RB(SHL,sum,shift),RC(3)),frac)));
   result=RS(RB(UGE,RB(SUB,exponent,shift),RC(255)),RB(OR,sign,RC(UINT32_C(0x7f7fffff))),result);
   result=RS(RB(ULE,exponent,shift),sign,result);
   result=RS(empty,RB(AND,RB(AND,a,b),signmask),result);
   result=RS(finite,result,native_result);
   return e && !e->failed?result:0;
}
static INLINE xe_rtz_id xe_rtz_emit_mul(xe_rtz_emitter *e,xe_rtz_id a,xe_rtz_id b,xe_rtz_id native_result) {
   xe_rtz_id z=RC(0),c23=RC(23),mask=RC(255);
   xe_rtz_id ea=RB(AND,RB(SHR,a,c23),mask),eb=RB(AND,RB(SHR,b,c23),mask);
   xe_rtz_id finite=RB(BOOL_AND,RB(ULT,ea,mask),RB(ULT,eb,mask));
   xe_rtz_id sign=RB(AND,RB(XOR,a,b),RC(UINT32_C(0x80000000)));
   xe_rtz_id frac=RC(UINT32_C(0x7fffff)),hidden=RC(UINT32_C(0x800000));
   xe_rtz_id sa=RB(OR,RB(AND,a,frac),hidden),sb=RB(OR,RB(AND,b,frac),hidden);
   /* Exact bits[47:16] of the significand product, from native 32x32 extended
    * multiply. Neither backend declares or requires 64-bit shader integers. */
   xe_rtz_id high=RB(MUL_TRUNC16,sa,sb);
   xe_rtz_id carry=RB(SHR,high,RC(31)),exponent=RB(ADD,RB(ADD,ea,eb),carry);
   xe_rtz_id result=RB(OR,sign,RB(OR,RB(SHL,RB(SUB,exponent,RC(127)),c23),RB(AND,RB(SHR,high,RB(ADD,RC(7),carry)),frac)));
   result=RS(RB(UGE,exponent,RC(382)),RB(OR,sign,RC(UINT32_C(0x7f7fffff))),result);
   result=RS(RB(ULE,exponent,RC(127)),sign,result);
   result=RS(RB(EQ,ea,z),sign,RS(RB(EQ,eb,z),sign,result));
   result=RS(finite,result,native_result);
   return e && !e->failed?result:0;
}
#undef RC
#undef RB
#undef RS
#endif
