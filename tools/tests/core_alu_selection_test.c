/* Independent selected-bit anchors and source modifiers. MSVC C89. */
#include "libretro/core_alu_operand.h"
#include <stdio.h>
#if defined(_M_X64) || defined(__x86_64__)
#include <xmmintrin.h>
#define XE_SELECTION_HOST_MODES 1
#endif
#define CHECK(x) do{if(!(x)){fprintf(stderr,"selection unit line %u\n",(unsigned)__LINE__);return 1;}}while(0)
int main(void)
{
 static const uint32_t words[4]={1,0x80000001u,0x7fc12345u,0x80000000u};
 static const uint32_t pairs[8][4]={
  {1,0,1,0},{0,1,0,1},{0x80000001u,0x80000000u,0x80000001u,0x80000000u},
  {0x80000000u,0x80000001u,0x80000000u,0x80000001u},
  {0,0x7fc12345u,0x7fc12345u,0x7fc12345u},
  {0x7fc12345u,1,1,1},{0x3f000000u,0x3f800000u,0x3f800000u,0x3f000000u},
  {0xbf800000u,0,0,0xbf800000u}};
 unsigned mode,swizzle,lane,absolute,negate,i;uint32_t wanted,bits;float value,vector[4];
#ifdef XE_SELECTION_HOST_MODES
 unsigned saved=_mm_getcsr();
#endif
 for(mode=0;mode<2;++mode){
#ifdef XE_SELECTION_HOST_MODES
  _mm_setcsr(mode?saved|0x8040u:saved&~0x8040u);
#endif
  for(i=0;i<8;++i){
   CHECK(xe_alu_minmax_bits(pairs[i][0],pairs[i][1],0)==pairs[i][2]);
   CHECK(xe_alu_minmax_bits(pairs[i][0],pairs[i][1],1)==pairs[i][3]);
  }
  for(i=0;i<8;++i){
   float a[4],b[4];unsigned which;
   for(lane=0;lane<4;++lane){memcpy(a+lane,&pairs[(i+lane)&7][0],4);memcpy(b+lane,&pairs[(i+lane)&7][1],4);}
   for(which=0;which<2;++which){
    xe_alu_minmax_vector(vector,a,b,(int)which);
    for(lane=0;lane<4;++lane){memcpy(&bits,vector+lane,4);CHECK(bits==pairs[(i+lane)&7][which+2]);}
   }
  }
  for(swizzle=0;swizzle<256;++swizzle)for(lane=0;lane<4;++lane)
  for(absolute=0;absolute<2;++absolute)for(negate=0;negate<2;++negate){
   wanted=words[(((swizzle>>(lane*2))&3)+lane)&3];
   if(absolute)wanted&=0x7fffffffu;
   if(negate)wanted^=0x80000000u;
   value=xe_alu_source_value_preserve(words,swizzle,lane,(int)absolute,(int)negate);
   memcpy(&bits,&value,4);CHECK(bits==wanted);
   xe_alu_source_vector_preserve(vector,words,swizzle,(int)absolute,(int)negate);
   memcpy(&bits,vector+lane,4);CHECK(bits==wanted);
  }
 }
#ifdef XE_SELECTION_HOST_MODES
 _mm_setcsr(saved);
#endif
 puts("PASS: raw selection anchors,8192 source/modifier checks, host FTZ/DAZ independence");return 0;
}
