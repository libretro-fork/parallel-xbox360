/* Ordered vertex dot arithmetic matching the existing renderer. BSD. C89. */
#ifndef XE_CORE_SHADER_VERTEX_DOT_H
#define XE_CORE_SHADER_VERTEX_DOT_H
#include "core_float_rtz.h"
static INLINE float xe_shader_vertex_product(float a,float b) {
 uint32_t ab,bb;memcpy(&ab,&a,4);memcpy(&bb,&b,4);
 /* SM3 suppresses a zero/denormal input, including multiplication by NaN/Inf.
  * A negative zero produced by nonzero underflow remains a negative zero. */
 if((ab&UINT32_C(0x7fffffff))<UINT32_C(0x800000) ||
    (bb&UINT32_C(0x7fffffff))<UINT32_C(0x800000))return 0.0f;
 return xe_float_mul_rtz(a,b);
}
/* lanes is a decoded2/3/4. A non-null third is DP2ADD's already resolved x.
 * Start at product0: a synthetic +0 prefix changes all-negative underflow.
 * No fused operation, reassociation, host rounding-mode change or allocation. */
static INLINE float xe_shader_vertex_dot_rtz(const float *a,const float *b,unsigned lanes,const float *third) {
 unsigned i;float sum=xe_shader_vertex_product(a[0],b[0]);
 for(i=1;i<lanes;++i)sum=xe_float_add_rtz(sum,xe_shader_vertex_product(a[i],b[i]));
 if(third)sum=xe_float_add_rtz(sum,*third);
 return sum;
}
#endif
