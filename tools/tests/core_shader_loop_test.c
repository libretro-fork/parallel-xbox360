/* C89 driver: hostile bounds plus real packed Xenos loop microcode. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libretro/core_shader_loop.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"loop line %d\n",__LINE__); exit(1); } } while (0)
static uint32_t pack(unsigned count, unsigned start, int step)
{ return count | (start << 8) | (((uint32_t)step & 255) << 16); }
static void unit(void)
{
   struct { uint32_t before, c[4], i[4], after; } s;
   uint32_t depth, d, count, start, it;
   int step, expected;
   memset(&s,0,sizeof(s)); s.before=123; s.after=456; depth=0;
   CHECK(xe_shader_loop_address(0,s.c,s.i)==0);
   for (d=0;d<4;++d) {
      CHECK(xe_shader_loop_enter(&depth,s.c,s.i,pack(2,10+d,-1),0));
      CHECK(depth==d+1 && xe_shader_loop_address(depth,s.c,s.i)==(int)(10+d));
   }
   CHECK(!xe_shader_loop_enter(&depth,s.c,s.i,pack(2,99,1),0));
   CHECK(depth==4 && s.before==123 && s.after==456);
   CHECK(xe_shader_loop_address(5,s.c,s.i)==0);
   for (d=4;d;--d) CHECK(xe_shader_loop_address(d,s.c,s.i)==(int)(9+d));
   depth=1; s.i[1]=99;
   CHECK(!xe_shader_loop_enter(&depth,s.c,s.i,pack(0,1,1),0) && depth==1 && s.i[1]==0);
   s.i[1]=1;
   CHECK(xe_shader_loop_enter(&depth,s.c,s.i,pack(3,7,-2),1));
   CHECK(depth==2 && s.i[1]==1 && xe_shader_loop_address(depth,s.c,s.i)==5);
   depth=1; s.i[1]=3;
   CHECK(!xe_shader_loop_enter(&depth,s.c,s.i,pack(3,7,-2),1) && depth==1);
   /* Every hardware start/step and representative iterator, including clamps. */
   for(start=0;start<256;++start) for(step=-128;step<128;++step)
      for(count=0;count<5;++count) {
         it=count==4?255:count; s.c[3]=pack(255,start,step); s.i[3]=it;
         expected=(int)it*step+(int)start;
         if(expected>256) expected=256;
         if(expected< -256) expected= -256;
         CHECK(xe_shader_loop_address(4,s.c,s.i)==expected);
      }
   s.i[3]=UINT32_MAX; s.c[3]=pack(255,255,-128);
   CHECK(xe_shader_loop_address(4,s.c,s.i)==-256);
}
#ifdef XE_LOOP_NATIVE
extern unsigned xe_loop_native(const uint32_t *,const uint32_t *,float *,unsigned,unsigned);
static uint32_t cf[64][2], code[128], constants[32];
static unsigned cf_count,expected_count;
static float expected[4096], actual[4096];
static void emit(unsigned op,uint32_t a,uint32_t b)
{ CHECK(cf_count<64); cf[cf_count][0]=a; cf[cf_count++][1]=(op<<12)|b; }
static void body(void) { emit(1,32|(1<<12),0); }
static void loop_code(unsigned level,unsigned levels,int breaking)
{
   unsigned begin=cf_count, end;
   emit(7,level<<16,0); body();
   if(level+1<levels) loop_code(level+1,levels,breaking);
   body(); end=cf_count;
   emit(8,(begin+1)|(level<<16)|(breaking?(1u<<21):0),0);
   cf[begin][0]|=end+1;
}
static void model(unsigned level,unsigned levels,int breaking)
{
   unsigned i,count=constants[level]&255;
   int step=(int)((constants[level]>>16)&255),address;
   if(step>=128) step-=256;
   if(level>=4) return;
   for(i=0;i<count;++i) {
      address=(int)((constants[level]>>8)&255)+(int)i*step;
      if(address>256) address=256;
      if(address< -256) address= -256;
      CHECK(expected_count<4094); expected[expected_count++]=(float)(address&63)+0.25f;
      if(level+1<levels) model(level+1,levels,breaking);
      expected[expected_count++]=(float)(address&63)+0.25f;
      if(breaking) break;
   }
}
static void native_case(unsigned levels,unsigned count,int step,int empty,int breaking)
{
   unsigned i,word,n;
   memset(code,0,sizeof(code)); memset(cf,0,sizeof(cf)); memset(constants,0,sizeof(constants));
   for(i=0;i<levels;++i) constants[i]=pack(empty && i==1?0:count,3+i,step);
   cf_count=0; loop_code(0,levels,breaking); emit(2,0,0);
   for(i=0;i<cf_count;i+=2) {
      word=(i/2)*3; code[word]=cf[i][0];
      code[word+1]=cf[i][1]|(cf[i+1][0]<<16);
      code[word+2]=(cf[i+1][0]>>16)|(cf[i+1][1]<<16);
   }
   /* max export0.x, r[aL].x, r[aL].x; scalar retain, identity swizzles. */
   code[96]=(1u<<15)|(1u<<16)|(50u<<26);
   code[97]=0;
   code[98]=(0x40u<<16)|(0x40u<<8)|(2u<<24)|(1u<<31)|(1u<<30);
   expected_count=0; model(0,levels,breaking);
   n=xe_loop_native(code,constants,actual,4096,2);
   CHECK(n==expected_count);
   for(i=0;i<n;++i) CHECK(actual[i]==expected[i]);
}
static void native(void)
{
   unsigned levels;
   for(levels=1;levels<=5;++levels) {
      native_case(levels,2,-1,0,0);
      native_case(levels,2,1,1,0);
      native_case(levels,3,-2,0,1);
   }
   native_case(1,255,127,0,0); native_case(1,255,-128,0,0);
   native_case(1,0,1,0,0);
}
#endif
int main(void)
{
   unit();
#ifdef XE_LOOP_NATIVE
   native();
#endif
   puts("PASS: bounded shader loop stack and aL addressing"); return 0;
}
