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
   struct { uint32_t before, c[4], n[4]; int32_t a[4]; uint32_t after; } s;
   uint32_t depth, d, start, it;
   int step,expected;
   memset(&s,0,sizeof(s)); s.before=123; s.after=456; depth=0;
   CHECK(xe_shader_loop_address(0,s.a)==0);
   for (d=0;d<4;++d) {
      CHECK(xe_shader_loop_enter(&depth,s.c,s.n,s.a,pack(2,10+d,-1),0));
      CHECK(depth==d+1 && xe_shader_loop_address(depth,s.a)==(int)(10+d));
   }
   CHECK(!xe_shader_loop_enter(&depth,s.c,s.n,s.a,pack(2,99,1),0));
   CHECK(depth==4 && s.before==123 && s.after==456);
   CHECK(xe_shader_loop_address(5,s.a)==0);
   for (d=4;d;--d) {
      CHECK(xe_shader_loop_advance(&depth,s.c,s.n,s.a,0));
      CHECK(xe_shader_loop_address(depth,s.a)==(int)(8+d));
      CHECK(!xe_shader_loop_advance(&depth,s.c,s.n,s.a,0) && depth==d-1);
   }
   CHECK(!xe_shader_loop_advance(&depth,s.c,s.n,s.a,0));
   CHECK(xe_shader_loop_enter(&depth,s.c,s.n,s.a,pack(2,5,2),0));
   CHECK(xe_shader_loop_advance(&depth,s.c,s.n,s.a,0));
   /* Reused slots must inherit current outer aL, not stale inner progress. */
   s.n[1]=99; s.a[1]=-123;
   CHECK(xe_shader_loop_enter(&depth,s.c,s.n,s.a,pack(3,99,-2),1));
   CHECK(depth==2 && s.n[1]==3 && xe_shader_loop_address(depth,s.a)==7);
   CHECK(xe_shader_loop_advance(&depth,s.c,s.n,s.a,0));
   CHECK(xe_shader_loop_address(depth,s.a)==5);
   CHECK(!xe_shader_loop_advance(&depth,s.c,s.n,s.a,1) && depth==1);
   CHECK(xe_shader_loop_address(depth,s.a)==7);
   /* Existing empty-loop no-push policy is retained, not a new hardware claim. */
   CHECK(!xe_shader_loop_enter(&depth,s.c,s.n,s.a,pack(0,1,1),0) && depth==1);
   CHECK(xe_shader_loop_address(depth,s.a)==7);
   depth=0;
   CHECK(xe_shader_loop_enter(&depth,s.c,s.n,s.a,pack(2,99,-1),1));
   CHECK(xe_shader_loop_address(depth,s.a)==0);
   /* All legal starts/steps, every address in a maximum-length loop. */
   for(start=0;start<256;++start) for(step=-128;step<128;++step) {
      depth=0;
      CHECK(xe_shader_loop_enter(&depth,s.c,s.n,s.a,pack(255,start,step),0));
      for(it=0;it<255;++it) {
         expected=(int)start+(int)it*step;
         if(expected>256)expected=256;
         if(expected< -256)expected= -256;
         CHECK(xe_shader_loop_address(depth,s.a)==expected);
         CHECK(xe_shader_loop_advance(&depth,s.c,s.n,s.a,0)==(it<254));
      }
      CHECK(!depth);
   }
   depth=1; s.n[0]=2; s.c[0]=pack(2,0,127); s.a[0]=INT32_MAX;
   CHECK(xe_shader_loop_advance(&depth,s.c,s.n,s.a,0) && s.a[0]==256);
   s.n[0]=2; s.c[0]=pack(2,0,-128); s.a[0]=INT32_MIN;
   CHECK(xe_shader_loop_advance(&depth,s.c,s.n,s.a,0) && s.a[0]==-256);
   depth=5; CHECK(!xe_shader_loop_advance(&depth,s.c,s.n,s.a,0) && depth==5);
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
      if(address>256)address=256;
      if(address< -256)address= -256;
      CHECK(expected_count<4094); expected[expected_count++]=(float)(address&63)+0.25f;
      if(level+1<levels) model(level+1,levels,breaking);
      expected[expected_count++]=(float)(address&63)+0.25f;
      if(breaking) break;
   }
}
static void finalize_code(void)
{
   unsigned i,word;
   for(i=0;i<cf_count;i+=2) {
      word=(i/2)*3; code[word]=cf[i][0];
      code[word+1]=cf[i][1]|(cf[i+1][0]<<16);
      code[word+2]=(cf[i+1][0]>>16)|(cf[i+1][1]<<16);
   }
   /* max export0.x, r[aL].x, r[aL].x; scalar retain, identity swizzles. */
   code[96]=(1u<<15)|(1u<<16)|(50u<<26);
   code[97]=0;
   code[98]=(0x40u<<16)|(0x40u<<8)|(2u<<24)|(1u<<31)|(1u<<30);
}
static void repeat_case(void)
{
   static const float wanted[8]={5.25f,4.25f,3.25f,5.25f,7.25f,6.25f,5.25f,7.25f};
   unsigned i,n;
   memset(code,0,sizeof(code)); memset(cf,0,sizeof(cf)); memset(constants,0,sizeof(constants));
   constants[0]=pack(2,5,2); constants[1]=pack(3,99,-1); cf_count=0;
   emit(7,6,0); emit(7,4|(1u<<13)|(1u<<16),0); body();
   emit(8,2|(1u<<16),0); body(); emit(8,1,0); emit(2,0,0);
   finalize_code(); n=xe_loop_native(code,constants,actual,4096,2);
   CHECK(n==8); for(i=0;i<n;++i) CHECK(actual[i]==wanted[i]);
}
static void native_case(unsigned levels,unsigned count,int step,int empty,int breaking)
{
   unsigned i,n;
   memset(code,0,sizeof(code)); memset(cf,0,sizeof(cf)); memset(constants,0,sizeof(constants));
   for(i=0;i<levels;++i) constants[i]=pack(empty && i==1?0:count,3+i,step);
   cf_count=0; loop_code(0,levels,breaking); emit(2,0,0);
   finalize_code();
   expected_count=0; model(0,levels,breaking);
   n=xe_loop_native(code,constants,actual,4096,2);
   CHECK(n==expected_count);
   for(i=0;i<n;++i) CHECK(actual[i]==expected[i]);
}
static void native(void)
{
   unsigned levels;
   repeat_case();
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
