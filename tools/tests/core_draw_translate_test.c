/* Calls the real instrumented translator on packed guest microcode. C89. */
#include "libretro/core_draw_stats.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"draw translate line %u\n",(unsigned)__LINE__);exit(1);}}while(0)
size_t xe_rtz_native_spirv(const uint32_t*,size_t,unsigned,unsigned,unsigned,uint32_t*,size_t);
static uint32_t binary[65536],reference[65536];
int main(int argc,char**argv){uint32_t code[6];size_t n,ref=0;unsigned role,previous;xe_draw_stats stats;(void)argv;
 code[0]=0x1001;code[1]=0x2000;code[2]=0;code[3]=62|(1u<<15)|(15u<<16)|(50u<<26);code[4]=0;code[5]=0xc0000100;
 for(role=XE_DRAW_INITIALIZATION;role<XE_DRAW_ROLE_COUNT;++role){
  xe_draw_stats_reset();previous=xe_draw_stats_enter(role);
  n=xe_rtz_native_spirv(code,6,0,1,1,binary,65536);xe_draw_stats_leave(previous);CHECK(n && binary[0]==0x07230203);
  CHECK(xe_draw_stats_snapshot(&stats,sizeof(stats)));CHECK(stats.events[role][XE_DRAW_GUEST_PIPELINE][XE_DRAW_TRANSLATE]==1);
  CHECK((xe_draw_stats_violations(&stats)!=0)==(role>=XE_DRAW_GPU));
  if(!ref){ref=n;memcpy(reference,binary,n*sizeof(*binary));}else CHECK(ref==n && !memcmp(reference,binary,n*sizeof(*binary)));
 }
 if(argc>2){puts("EXPECTED FAILURE: real draw-side translation rejected");return xe_draw_stats_violations(&stats)?1:0;}
 puts("PASS: actual translator instrumented at emission entry; exact bytes across initialization/worker/critical roles, critical roles fail gate");return 0;
}
