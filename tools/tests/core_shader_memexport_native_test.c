/* Raw guest microcode fixtures and expected masks. MSVC C89. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core_spv_memexport_program.h"
unsigned xe_memexport_native_check(const uint32_t*,size_t,unsigned,const uint8_t*,size_t,unsigned,unsigned,const char*);
unsigned xe_memexport_unanalyzed_rejection(void);
#define CHECK(x) do { if(!(x)){fprintf(stderr,"native memexport line%u result%u\n",(unsigned)__LINE__,result);exit(1);} } while(0)
static uint32_t code[32];
static uint8_t before[8];
static void cf(unsigned index,uint32_t low,unsigned opcode)
{
 uint32_t *p=code+index/2*3;
 if(index&1){p[1]|=low<<16;p[2]=(low>>16)|(opcode<<28);}
 else{p[0]=low;p[1]=opcode<<12;}
}
static void graph(void)
{
 memset(code,0,sizeof(code));memset(before,0,sizeof(before));
 cf(0,4,12);code[1]|=0x600; /* alloc export */
 cf(1,4|(2u<<12),1);cf(2,5,11);cf(3,0,0);cf(4,3|8192u,11);
 cf(5,7,2);cf(6,6|(1u<<12),2);cf(7,0,0);
 /* eA = mad r0,c0,c1; eM0 and eM1 = max r0,r0. */
 code[12]=32u|(1u<<15)|(15u<<16)|(50u<<26);code[13]=0;code[14]=1u|(11u<<24)|(1u<<31);
 code[15]=33u|(1u<<15)|(15u<<16)|(50u<<26);code[16]=0;code[17]=(2u<<24)|(3u<<30);
 code[18]=34u|(1u<<15)|(15u<<16)|(50u<<26);code[19]=0;code[20]=(2u<<24)|(3u<<30);
 before[2]=before[3]=before[4]=before[5]=1;
}
int main(int argc,char **argv)
{
 unsigned pixel,mask,extra,result=0,cases=0;size_t count;char path[2048];
 CHECK(argc==2);result=xe_memexport_unanalyzed_rejection();CHECK(!result);
 for(pixel=0;pixel<2;++pixel){
  for(mask=0;mask<32;++mask){
   count=xe_spv_memexport_test_program(code,mask);memset(before,0,sizeof(before));
   sprintf(path,"%s/linear-s%u-m%u.spv",argv[1],pixel,mask);
   result=xe_memexport_native_check(code,count,pixel,before,mask?2:0,mask,0,path);CHECK(!result);++cases;
  }
  graph();sprintf(path,"%s/pending-branch-s%u.spv",argv[1],pixel);
  result=xe_memexport_native_check(code,21,pixel,before,8,3,0,path);CHECK(!result);++cases;
  /* An actual last EXEC without END must flush at the implicit end boundary. */
  count=xe_spv_memexport_test_program(code,1);code[2]=(code[2]&0x0fffffff)|0x10000000;memset(before,0,sizeof(before));
  sprintf(path,"%s/fallthrough-s%u.spv",argv[1],pixel);
  result=xe_memexport_native_check(code,count,pixel,before,2,1,0,path);CHECK(!result);++cases;
  graph();code[3]=20; /* CF2 branch target beyond the analyzed CF span. */
  result=xe_memexport_native_check(code,21,pixel,before,8,0,1,NULL);CHECK(!result);++cases;
  /* Keep all native-profile cases; add portable software-FP modules for GPU
   * devices without the optional all-features native float-control profile. */
  for(extra=0;extra<3;++extra){
   mask=extra==2?31:extra;count=xe_spv_memexport_test_program(code,mask);memset(before,0,sizeof(before));
   sprintf(path,"%s/portable-linear-s%u-m%u.spv",argv[1],pixel,mask);
   result=xe_memexport_native_check(code,count,pixel|2u,before,mask?2:0,mask,0,path);CHECK(!result);++cases;
  }
  graph();sprintf(path,"%s/portable-pending-branch-s%u.spv",argv[1],pixel);
  result=xe_memexport_native_check(code,21,pixel|2u,before,8,3,0,path);CHECK(!result);++cases;
  count=xe_spv_memexport_test_program(code,1);code[2]=(code[2]&0x0fffffff)|0x10000000;memset(before,0,sizeof(before));
  sprintf(path,"%s/portable-fallthrough-s%u.spv",argv[1],pixel);
  result=xe_memexport_native_check(code,count,pixel|2u,before,2,1,0,path);CHECK(!result);++cases;
 }
 printf("PASS: %u actual vertex/pixel memexport analyses/translations; malformed and unanalyzed claimant failure published\n",cases);return 0;
}
