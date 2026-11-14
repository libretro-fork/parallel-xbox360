/* Literal ordered products/sums under varied host FP modes. MSVC C89. */
#include "libretro/core_shader_vertex_dot.h"
#include <stdio.h>
#include <stdlib.h>
#if defined(_M_X64) || defined(__x86_64__)
#include <xmmintrin.h>
#define XE_DOT_HOST_MODES 16
#else
#define XE_DOT_HOST_MODES 1
#endif
#define CHECK(x) do{if(!(x)){fprintf(stderr,"vertex dot line %u\n",(unsigned)__LINE__);exit(1);}}while(0)
static const uint32_t inputs[10][12]={
 {0x00800000u,0x00800000u,0x00800000u,0x00800000u,0xbf000000u,0xbf000000u,0xbf000000u,0xbf000000u,0x80000000u,0,0,0},
 {0x80000000u,0x80000000u,0x80000000u,0x80000000u,0x7f800000u,0x7f800000u,0x7f800000u,0x7f800000u,0x80000000u,0,0,0},
 {0x007fffffu,0x007fffffu,0x007fffffu,0x007fffffu,0xff800000u,0xff800000u,0xff800000u,0xff800000u,0x80000000u,0,0,0},
 {0x3f800000u,0x33c00000u,0x33c00000u,0x33c00000u,0x3f800000u,0x3f800000u,0x3f800000u,0x3f800000u,0x33c00000u,0,0,0},
 {0x3f800000u,0xbf800000u,0x40000000u,0xc0000000u,0x3f800000u,0x3f800000u,0x3f800000u,0x3f800000u,0xbf000000u,0,0,0},
 {0x7f800000u,0xff800000u,0,0,0x3f800000u,0x3f800000u,0x7f800000u,0x7f800000u,0,0,0,0},
 {0x80800000u,0x80800000u,0x80800000u,0x80800000u,0x3f000000u,0x3f000000u,0x3f000000u,0x3f000000u,0,0,0,0},
 {0x00800000u,0x00800000u,0x00800000u,0x00800000u,0xbf000000u,0x3f000000u,0xbf000000u,0x3f000000u,0x80000000u,0,0,0},
 {0x80000000u,0x80000000u,0x80000000u,0x80000000u,0xff800000u,0xff800000u,0xff800000u,0xff800000u,0x80000001u,0,0,0},
 {0x7f7fffffu,0x7f7fffffu,0x7f7fffffu,0x7f7fffffu,0x3f800000u,0x3f800000u,0x3f800000u,0x3f800000u,0,0,0,0}
};
static const uint32_t expected[10][3]={
 {0x80000000u,0x80000000u,0x80000000u},{0,0,0},{0,0,0},
 {0x3f800000u,0x3f800000u,0x3f800000u},{0,0x40000000u,0xbf000000u},
 {0x7fc00000u,0x7fc00000u,0x7fc00000u},{0x80000000u,0x80000000u,0},
 {0,0,0},{0,0,0},{0x7f7fffffu,0x7f7fffffu,0x7f7fffffu}
};
int main(void){
 unsigned which,op,mode,cases=0;float values[12],result;uint32_t bits,golden;
#if XE_DOT_HOST_MODES > 1
 unsigned original=_mm_getcsr(),control;
#endif
 for(mode=0;mode<XE_DOT_HOST_MODES;++mode){
#if XE_DOT_HOST_MODES > 1
  control=(original&~0xe040u)|0x1f80u|((mode&3)<<13)|((mode&4)?0x8000u:0)|((mode&8)?0x40u:0);
  _mm_setcsr(control);
#endif
  for(which=0;which<10;++which)for(op=0;op<3;++op){
   memcpy(values,inputs[which],sizeof(values));
   result=xe_shader_vertex_dot_rtz(values,values+4,4-op,op==2?values+8:NULL);
#if XE_DOT_HOST_MODES > 1
   CHECK((_mm_getcsr()&0xe040u)==(control&0xe040u));
#endif
   memcpy(&bits,&result,4);golden=expected[which][op];
   CHECK(bits==golden || ((bits&0x7fffffffu)>0x7f800000u && golden==0x7fc00000u));++cases;
  }
 }
#if XE_DOT_HOST_MODES > 1
 _mm_setcsr(original);
#endif
 printf("PASS: vertex dot literal cases=%u host_modes=%u; ordered RTZ/SM3, signed underflow, nonfinite classes, no host FP-mode dependency\n",cases,(unsigned)XE_DOT_HOST_MODES);return 0;
}
