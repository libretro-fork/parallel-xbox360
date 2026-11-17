/* Literal native-audited anchors and independent double comparison oracle. C89. */
#include "libretro/core_alu_operand.h"
#include "shader_conditional_fixture.h"
#include <stdio.h>
#include <stdlib.h>
#if defined(_M_X64) || defined(__SSE2__)
#include <xmmintrin.h>
#endif
#define CHECK(x) do{if(!(x)){fprintf(stderr,"conditional unit line%u\n",(unsigned)__LINE__);exit(1);}}while(0)
#ifdef XE_ALU_CONDITIONAL_TEST_DISPATCH
#define select_words xe_alu_conditional_dispatch
#else
#define select_words xe_alu_conditional_vector
#endif
static uint32_t oracle(uint32_t condition,uint32_t yes,uint32_t no,unsigned mode) {
 uint32_t clean=condition;float value;double wide;int take=0;
 if((clean&0x7fffffffu)<0x00800000u)clean&=0x80000000u;
 memcpy(&value,&clean,4);wide=value;
 if(mode==0)take=wide==0.0;else if(mode==1)take=wide>=0.0;else if(mode==2)take=wide>0.0;
 return take?yes:no;
}
static void check_words(const uint32_t *input,const uint32_t *expected,unsigned mode) {
 float a[4],b[4],c[4],out[4];uint32_t observed[4];unsigned lane,alias;
 for(alias=0;alias<4;++alias){
  memcpy(a,input,16);memcpy(b,input+4,16);memcpy(c,input+8,16);
  if(alias==0){select_words(out,a,b,c,mode);memcpy(observed,out,16);}
  else if(alias==1){select_words(a,a,b,c,mode);memcpy(observed,a,16);}
  else if(alias==2){select_words(b,a,b,c,mode);memcpy(observed,b,16);}
  else{select_words(c,a,b,c,mode);memcpy(observed,c,16);}
  for(lane=0;lane<4;++lane){
   CHECK(observed[lane]==expected[lane]);
   CHECK(xe_alu_conditional_bits(input[lane],input[4+lane],input[8+lane],mode)==expected[lane]);
  }
 }
}
/* Literal swizzle maps and modifier results, independent of the helper's bit
 * arithmetic. Output is disjoint from the borrowed immutable source. */
static void check_source_modifiers(void) {
 static const uint32_t input[4]={0x80000001u,0xffc12345u,1u,0x7f800000u};
 static const uint32_t expected[4][4]={
  {0x80000001u,0xffc12345u,1u,0x7f800000u},
  {1u,0x7fc12345u,1u,0x7f800000u},
  {1u,0x7fc12345u,0x80000001u,0xff800000u},
  {0x80000001u,0xffc12345u,0x80000001u,0xff800000u}};
 static const uint32_t swizzle[6]={0u,0x55u,0xaau,0xffu,0xe4u,0x1bu};
 static const unsigned map[6][4]={{0,1,2,3},{1,2,3,0},{2,3,0,1},
  {3,0,1,2},{0,2,0,2},{3,3,3,3}};
 float output[4],value;uint32_t words[4],word;unsigned which,mode,lane;
 for(which=0;which<6;++which)for(mode=0;mode<4;++mode){
  xe_alu_source_vector_preserve(output,input,swizzle[which],mode&1,mode>>1);
  memcpy(words,output,sizeof(words));
  for(lane=0;lane<4;++lane){
   CHECK(words[lane]==expected[mode][map[which][lane]]);
   value=xe_alu_source_value_preserve(input,swizzle[which],lane,mode&1,mode>>1);
   memcpy(&word,&value,4);CHECK(word==expected[mode][map[which][lane]]);
  }
 }
 xe_alu_source_vector_preserve(output,NULL,0xffu,1,1);
 memcpy(words,output,sizeof(words));for(lane=0;lane<4;++lane)CHECK(!words[lane]);
 value=xe_alu_source_value_preserve(input,0u,4,1,1);
 memcpy(&word,&value,4);CHECK(!word);
}
int main(void) {
 uint32_t input[16],expected[4],random=1;unsigned which,lane,mode,round,host;
#if defined(_M_X64) || defined(__SSE2__)
 unsigned saved=_mm_getcsr();
#endif
 for(host=0;host<2;++host){
#if defined(_M_X64) || defined(__SSE2__)
  _mm_setcsr(host?saved|0x8040u:saved&~0x8040u);
#endif
  check_source_modifiers();
  for(which=0;which<84;++which){
   conditional_inputs(input,which);
   for(lane=0;lane<4;++lane)expected[lane]=conditional_golden(which,lane);
   check_words(input,expected,which%3);
  }
  for(round=0;round<65536;++round){
   for(lane=0;lane<12;++lane){random=random*1664525u+1013904223u;input[lane]=random;}
   for(mode=0;mode<4;++mode){
#if defined(_M_X64) || defined(__SSE2__)
    _mm_setcsr(saved&~0x8040u);
#endif
    for(lane=0;lane<4;++lane)expected[lane]=oracle(input[lane],input[4+lane],input[8+lane],mode);
#if defined(_M_X64) || defined(__SSE2__)
    _mm_setcsr(host?saved|0x8040u:saved&~0x8040u);
#endif
    check_words(input,expected,mode);
   }
  }
 }
#if defined(_M_X64) || defined(__SSE2__)
 _mm_setcsr(saved);
#endif
 puts("PASS: conditional raw selection,84 literal programs,131072 random vectors, double oracle, host-mode and exact-alias controls");return 0;
}
