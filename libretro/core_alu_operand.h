/* Borrowed shader source words and bit-exact modifiers. BSD license. C89. */
#ifndef XE_CORE_ALU_OPERAND_H
#define XE_CORE_ALU_OPERAND_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <float.h>
#include <retro_inline.h>
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
