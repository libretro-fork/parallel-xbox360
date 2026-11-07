/* Binary32 sums/products, RTZ with signed input/output FTZ. MSVC C89.
 * Integer-only finite path: no host rounding mode, double or fma dependency.
 * NaN/infinity behavior is supplied by the caller's ordinary operation;
 * shader-model-3 zero multiplication is applied by the existing ALU caller. */
#ifndef XE_CORE_FLOAT_RTZ_H
#define XE_CORE_FLOAT_RTZ_H
#include <stdint.h>
#include <string.h>
#include <float.h>
#include "../third_party/libretro-common/include/retro_inline.h"
typedef char xe_float_rtz_binary32[(sizeof(float)==4 && FLT_MANT_DIG==24 && FLT_MAX_EXP==128 && FLT_RADIX==2)?1:-1];
static INLINE unsigned xe_float_rtz_clz(uint32_t x) {
   unsigned n=0;
   if(!x)return 32;
   if(!(x&UINT32_C(0xffff0000))){n+=16;x<<=16;}
   if(!(x&UINT32_C(0xff000000))){n+=8;x<<=8;}
   if(!(x&UINT32_C(0xf0000000))){n+=4;x<<=4;}
   if(!(x&UINT32_C(0xc0000000))){n+=2;x<<=2;}
   if(!(x&UINT32_C(0x80000000)))++n;
   return n;
}
static INLINE uint32_t xe_float_add_rtz_bits(uint32_t a,uint32_t b,uint32_t native_result) {
   uint32_t aa=a&UINT32_C(0x7fffffff),bb=b&UINT32_C(0x7fffffff);
   uint32_t ea=aa>>23,eb=bb>>23,sign,sa,sb,diff,shift,jam,sum,carry,exponent;
   if(ea==255 || eb==255)return native_result;
   if(!ea)aa=0;
   if(!eb)bb=0;
   /* Order by magnitude before aligning; retain three low guard/sticky bits.
    * Cancellation needing more than one normalization bit has no lost bits. */
   if(aa<bb){uint32_t t=a;a=b;b=t;t=aa;aa=bb;bb=t;t=ea;ea=eb;eb=t;}
   sign=a&UINT32_C(0x80000000);
   sa=ea?((aa&UINT32_C(0x7fffff))|UINT32_C(0x800000))<<3:0;
   sb=eb?((bb&UINT32_C(0x7fffff))|UINT32_C(0x800000))<<3:0;
   diff=ea-eb;shift=diff>31?31:diff;
   jam=diff && (sb<<((32-shift)&31))!=0;
   sb=(sb>>shift)|jam;
   sum=((a^b)&UINT32_C(0x80000000))?sa-sb:sa+sb;
   if(!sum)return (a&b)&UINT32_C(0x80000000);
   carry=sum>>27;
   if(carry)sum=(sum>>1)|(sum&1);
   shift=xe_float_rtz_clz(sum)-5;
   exponent=ea+carry;
   if(exponent<=shift)return sign;
   exponent-=shift;sum<<=shift;
   if(exponent>=255)return sign|UINT32_C(0x7f7fffff);
   return sign|(exponent<<23)|((sum>>3)&UINT32_C(0x7fffff));
}
static INLINE uint32_t xe_float_mul_rtz_bits(uint32_t a,uint32_t b,uint32_t native_result) {
   uint32_t ea=(a>>23)&255,eb=(b>>23)&255;
   uint32_t sign=(a^b)&UINT32_C(0x80000000),sa,sb,high,carry,exponent;
   if(ea==255 || eb==255)return native_result;
   if(!ea || !eb)return sign;
   sa=(a&UINT32_C(0x7fffff))|UINT32_C(0x800000);
   sb=(b&UINT32_C(0x7fffff))|UINT32_C(0x800000);
   /* Hosts are 64-bit; one full product retains all required significant bits.
    * The GPU recipe uses a 32x32 extended product without shader int64. */
   high=(uint32_t)(((uint64_t)sa*sb)>>16);
   carry=high>>31;exponent=ea+eb+carry;
   if(exponent<=127)return sign;
   if(exponent>=382)return sign|UINT32_C(0x7f7fffff);
   return sign|((exponent-127)<<23)|((high>>(7+carry))&UINT32_C(0x7fffff));
}
/* CPU interpreter boundary. Finite arithmetic never changes host FP state. */
static INLINE float xe_float_add_rtz(float a,float b) {
   uint32_t ab,bb,out;float result;
   memcpy(&ab,&a,4);memcpy(&bb,&b,4);
   if((ab&UINT32_C(0x7f800000))==UINT32_C(0x7f800000) ||
      (bb&UINT32_C(0x7f800000))==UINT32_C(0x7f800000))return a+b;
   out=xe_float_add_rtz_bits(ab,bb,0);memcpy(&result,&out,4);return result;
}
static INLINE float xe_float_mul_rtz(float a,float b) {
   uint32_t ab,bb,out;float result;
   memcpy(&ab,&a,4);memcpy(&bb,&b,4);
   if((ab&UINT32_C(0x7f800000))==UINT32_C(0x7f800000) ||
      (bb&UINT32_C(0x7f800000))==UINT32_C(0x7f800000))return a*b;
   out=xe_float_mul_rtz_bits(ab,bb,0);memcpy(&result,&out,4);return result;
}
#endif
