/* Semantic-equal finite guest programs, setup outside timing. MSVC C89. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "shader_compile_bench.h"
void *xe_selection_cpu_create(void);
void xe_selection_cpu_setup(void*,const uint32_t*,const uint32_t*);
void xe_selection_cpu_step(void*);
const uint32_t *xe_selection_cpu_result(void*,unsigned*);
void xe_selection_cpu_destroy(void*);
#define CHECK(x) do{if(!(x)){fprintf(stderr,"selection CPU bench line %u\n",(unsigned)__LINE__);exit(1);}}while(0)
int main(void)
{
 static const uint32_t values[12]={0x40000000u,0xc0000000u,0x3f000000u,0xbf000000u,
  0x3f800000u,0xbf800000u,0x3e800000u,0xbe800000u,0x3f800000u,0,0,0x40000000u};
 static const uint32_t vector_expected[2][4]={
  {0x40000000u,0xbf800000u,0x3f000000u,0xbe800000u},
  {0x3f800000u,0xc0000000u,0x3e800000u,0xbf000000u}};
 static const uint32_t arithmetic_expected[2][4]={
  {0x40400000u,0xc0400000u,0x3f400000u,0xbf400000u},
  {0x40000000u,0x40000000u,0x3e000000u,0x3e000000u}};
 static const uint32_t conditional_values[12]={0x80000000u,0,0x40000000u,0xc0000000u,
  0x3f800000u,0xbf800000u,0x3e800000u,0xbe800000u,
  0x40000000u,0x40400000u,0x40800000u,0x40a00000u};
 static const uint32_t conditional_expected[3][4]={
  {0x3f800000u,0xbf800000u,0x40800000u,0x40a00000u},
  {0x3f800000u,0xbf800000u,0x3e800000u,0x40a00000u},
  {0x40000000u,0x40400000u,0x3e800000u,0x40a00000u}};
 uint32_t code[6],expected[4];const uint32_t *result;void *owner;unsigned which,i,lane,count,scalar,minimum,temp,iterations,timed;
 timed=getenv("XE_SHADER_BENCH_CSV")!=NULL;iterations=timed?1000000u:1u;
 owner=xe_selection_cpu_create();CHECK(owner);
 /* Vector/scalar coissue borrows one final result. Both banks must observe
  * the original sources before the combined export commits either result. */
 for(which=0;which<4;++which){
  minimum=which&1;temp=which>=2;
  memset(code,0,sizeof(code));code[0]=1|(1u<<12);code[1]=0x2000;
  code[3]=62|(1u<<15)|(5u<<16)|(10u<<20)|((minimum?2u:0u)<<26);
  code[5]=2u|(1u<<8)|(minimum<<24)|(temp?0xe0000000u:0);
  xe_selection_cpu_setup(owner,code,values);xe_selection_cpu_step(owner);
  result=xe_selection_cpu_result(owner,&count);CHECK(count==1);
  for(lane=0;lane<4;++lane)CHECK(result[lane]==((lane&1)?
    (minimum?0x40000000u:0x40400000u):arithmetic_expected[minimum][lane]));
 }
 printf("PASS: CPU vector/scalar coissue cases=4; combined export and source banks\n");
 for(which=0;which<12;++which){
  scalar=(which&2)!=0;minimum=which&1;temp=(which&4)!=0;
  memset(code,0,sizeof(code));code[0]=1|(1u<<12);code[1]=0x2000;
  code[3]=62|(1u<<15)|(scalar?15u<<20:15u<<16)|((scalar?5u+minimum:50u)<<26);
  code[5]=scalar?2u|(temp?0x20000000u:0):
   (1u<<8)|((2u+minimum)<<24)|(temp?0xc0000000u:0);
  for(lane=0;lane<4;++lane)expected[lane]=scalar?(minimum?0x3f800000u:0x40000000u):vector_expected[minimum][lane];
  if(which>=8){
   scalar=which>=10;
   code[3]=62|(1u<<15)|(scalar?15u<<20:15u<<16)|((scalar?(which==10?0u:2u):50u)<<26);
   code[5]=scalar?2u:(1u<<8)|((which==9?1u:0u)<<24);
   for(lane=0;lane<4;++lane)expected[lane]=scalar?(which==10?0x40400000u:0x40000000u):arithmetic_expected[which-8][lane];
  }
  xe_selection_cpu_setup(owner,code,values);xe_selection_cpu_step(owner);
  result=xe_selection_cpu_result(owner,&count);CHECK(count==1);
  for(lane=0;lane<4;++lane)CHECK(result[lane]==expected[lane]);
  if(timed)XE_COMPILE_BEGIN();
  for(i=0;i<iterations;++i)xe_selection_cpu_step(owner);
  if(timed)XE_COMPILE_END("shader.cpu.selection_1m",which);
  result=xe_selection_cpu_result(owner,&count);CHECK(count==iterations+1);
  for(lane=0;lane<4;++lane)CHECK(result[lane]==expected[lane]);
 }
 /* Same finite outputs on the original and corrected CND implementations.
  * Cover all three comparisons through both temporary and constant banks. */
 for(which=0;which<6;++which){
  minimum=which%3;temp=which>=3;
  memset(code,0,sizeof(code));code[0]=1|(1u<<12);code[1]=0x2000;
  code[3]=62|(1u<<15)|(15u<<16)|(50u<<26);
  code[5]=2u|(1u<<8)|((12u+minimum)<<24)|(temp?0xe0000000u:0);
  xe_selection_cpu_setup(owner,code,conditional_values);xe_selection_cpu_step(owner);
  result=xe_selection_cpu_result(owner,&count);CHECK(count==1);
  for(lane=0;lane<4;++lane)CHECK(result[lane]==conditional_expected[minimum][lane]);
  if(timed)XE_COMPILE_BEGIN();
  for(i=0;i<iterations;++i)xe_selection_cpu_step(owner);
  if(timed)XE_COMPILE_END("shader.cpu.conditional_1m",which);
  result=xe_selection_cpu_result(owner,&count);CHECK(count==iterations+1);
  for(lane=0;lane<4;++lane)CHECK(result[lane]==conditional_expected[minimum][lane]);
 }
 xe_selection_cpu_destroy(owner);
 printf("PASS: CPU conditional cases=6 iterations=%u timed=%u; equal finite guest outputs\n",iterations,timed);
 printf("PASS: CPU selection cases=12 iterations=%u timed=%u; equal finite guest outputs\n",iterations,timed);return 0;
}
