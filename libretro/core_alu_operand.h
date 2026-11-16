/* Borrowed shader source words and bit-exact modifiers. BSD license. C89. */
#ifndef XE_CORE_ALU_OPERAND_H
#define XE_CORE_ALU_OPERAND_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <float.h>
#include <retro_inline.h>
#if !defined(XE_ALU_SELECTION_FORCE_SCALAR) && (defined(_M_X64) || defined(__SSE2__) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2))
#include <emmintrin.h>
#define XE_ALU_SELECTION_SIMD_SSE2 1
#endif
typedef char xe_alu_operand_binary32[(sizeof(float)==4 && FLT_MANT_DIG==24 && FLT_MAX_EXP==128 && FLT_RADIX==2)?1:-1];
/* Bank owns 512 vec4 constants and remains immutable during the borrow.
 * size_minus_one is the guest window, in vec4 units. No clamping or wrapping. */
static INLINE const uint32_t *xe_alu_constant_view(const uint32_t *bank,
 uint32_t address,int relative,int32_t relative_offset,uint32_t base,
 uint32_t size_minus_one) {
 int64_t index=(int64_t)address;
 if(relative)index+=(int64_t)relative_offset;
 if(!bank || index<0 || (uint64_t)index>size_minus_one ||
  (uint64_t)index+(uint64_t)base>=512)return NULL;
 return bank+((size_t)index+base)*4;
}
/* Selection operands retain payload bits; only their comparison view is FTZ. */
static INLINE float xe_alu_source_value_preserve(const void *source,
 uint32_t swizzle,unsigned lane,int absolute,int negate) {
 uint32_t bits,component;float value;
 if(!source || lane>=4)return 0.0f;
 component=((swizzle>>(lane*2))+lane)&3;
 memcpy(&bits,(const unsigned char*)source+component*4,4);
 bits=(bits&~((uint32_t)(absolute!=0)<<31))^((uint32_t)(negate!=0)<<31);
 memcpy(&value,&bits,4);return value;
}
static INLINE void xe_alu_source_vector_preserve(float *output,const void *source,
 uint32_t swizzle,int absolute,int negate) {
 unsigned lane;
 for(lane=0;lane<4;++lane)output[lane]=xe_alu_source_value_preserve(source,swizzle,lane,absolute,negate);
}
static INLINE uint32_t xe_alu_minmax_bits(uint32_t a,uint32_t b,int minimum) {
 uint32_t ma=a&UINT32_C(0x7fffffff),mb=b&UINT32_C(0x7fffffff);
 unsigned equal,less=0;
 if(ma>UINT32_C(0x7f800000) || mb>UINT32_C(0x7f800000))return b;
 if(ma<UINT32_C(0x00800000))ma=0;
 if(mb<UINT32_C(0x00800000))mb=0;
 equal=ma==mb && (!ma || (a>>31)==(b>>31));
 if(!equal)less=(a>>31)!=(b>>31)?(a>>31):(a>>31)?ma>mb:ma<mb;
 return (minimum?less:!less)?a:b;
}
static INLINE float xe_alu_minmax_value(float a,float b,int minimum) {
 uint32_t av,bv,bits;float value;
 memcpy(&av,&a,4);memcpy(&bv,&b,4);bits=xe_alu_minmax_bits(av,bv,minimum);
 memcpy(&value,&bits,4);return value;
}
/* Existing four-lane operands are borrowed in place; no staging vector. */
static INLINE void xe_alu_minmax_vector(float *output,const float *a,
 const float *b,int minimum) {
#ifdef XE_ALU_SELECTION_SIMD_SSE2
 __m128i av=_mm_castps_si128(_mm_loadu_ps(a)),bv=_mm_castps_si128(_mm_loadu_ps(b));
 __m128i zero=_mm_setzero_si128(),ones=_mm_set1_epi32(-1);
 __m128i ma=_mm_and_si128(av,_mm_set1_epi32(0x7fffffff));
 __m128i mb=_mm_and_si128(bv,_mm_set1_epi32(0x7fffffff));
 __m128i different,negative,less,choose,nan;
 nan=_mm_or_si128(_mm_cmpgt_epi32(ma,_mm_set1_epi32(0x7f800000)),
  _mm_cmpgt_epi32(mb,_mm_set1_epi32(0x7f800000)));
 ma=_mm_andnot_si128(_mm_cmpgt_epi32(_mm_set1_epi32(0x00800000),ma),ma);
 mb=_mm_andnot_si128(_mm_cmpgt_epi32(_mm_set1_epi32(0x00800000),mb),mb);
 negative=_mm_srai_epi32(av,31);different=_mm_srai_epi32(_mm_xor_si128(av,bv),31);
 less=_mm_or_si128(_mm_and_si128(negative,_mm_cmpgt_epi32(ma,mb)),
  _mm_andnot_si128(negative,_mm_cmpgt_epi32(mb,ma)));
 less=_mm_or_si128(_mm_and_si128(different,negative),_mm_andnot_si128(different,less));
 less=_mm_andnot_si128(_mm_cmpeq_epi32(_mm_or_si128(ma,mb),zero),less);
 choose=_mm_andnot_si128(nan,minimum?less:_mm_xor_si128(less,ones));
 _mm_storeu_ps(output,_mm_castsi128_ps(_mm_or_si128(_mm_and_si128(choose,av),_mm_andnot_si128(choose,bv))));
#else
 unsigned lane;
 for(lane=0;lane<4;++lane)output[lane]=xe_alu_minmax_value(a[lane],b[lane],minimum);
#endif
}
/* Source is four readable binary32 lanes (float or uint32_t storage).
 * Load only the selected lane; byte access avoids effective-type aliasing. */
static INLINE float xe_alu_source_value(const void *source,uint32_t swizzle,
 unsigned lane,int absolute,int negate) {
 uint32_t bits,component;float value;
 if(!source || lane>=4)return 0.0f;
 component=((swizzle>>(lane*2))+lane)&3;
 memcpy(&bits,(const unsigned char*)source+component*4,4);
 if(!(bits&UINT32_C(0x7f800000)))bits&=UINT32_C(0x80000000);
 bits=(bits&~((uint32_t)(absolute!=0)<<31))^((uint32_t)(negate!=0)<<31);
 memcpy(&value,&bits,4);return value;
}
/* Four-lane operand snapshot. Input and output do not overlap. Hoist shared
 * modifiers once; scalar reads retain their selected-lane interface. */
static INLINE void xe_alu_source_vector(float *output,const void *source,
 uint32_t swizzle,int absolute,int negate) {
 uint32_t lane,bits,mask=~((uint32_t)(absolute!=0)<<31);
 uint32_t sign=(uint32_t)(negate!=0)<<31;
 if(!source){memset(output,0,4*sizeof(float));return;}
 for(lane=0;lane<4;++lane){
  uint32_t component=((swizzle>>(lane*2))+lane)&3;
  memcpy(&bits,(const unsigned char*)source+component*4,4);
  if(!(bits&UINT32_C(0x7f800000)))bits&=UINT32_C(0x80000000);
  bits=(bits&mask)^sign;
  memcpy(output+lane,&bits,4);
 }
}
#endif
