/* Literal wrap goldens and actual packed full/mini interpreter instructions. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libretro/core_vertex_address.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"vertex address line %d\n",__LINE__); exit(1); } } while (0)
extern unsigned xe_vertex_address_native(const uint32_t *, uint32_t, uint32_t *);

static void addresses(void)
{
   static const struct { uint32_t base; int32_t offset; uint32_t result; } cases[] = {
      {0,0,0}, {0,-1,UINT32_C(0xffffffff)},
      {UINT32_C(0xffffffff),1,0},
      {UINT32_C(0x7fffffff),1,UINT32_C(0x80000000)},
      {UINT32_C(0x80000000),-1,UINT32_C(0x7fffffff)},
      {UINT32_C(0x7fffff00),256,UINT32_C(0x80000000)},
      {UINT32_C(0x803fffff),-4194304,UINT32_C(0x7fffffff)},
      {UINT32_C(0x7fc00001),4194303,UINT32_C(0x80000000)},
      {UINT32_C(0xffffffff),4194303,UINT32_C(0x003ffffe)},
      {0,-4194304,UINT32_C(0xffc00000)}
   };
   uint32_t seed=UINT32_C(0x641f923b),base,encoded,expected;
   int32_t offset;
   unsigned i;
   uint64_t wide;
   for(i=0;i<sizeof(cases)/sizeof(cases[0]);++i)
      CHECK(xe_vertex_fetch_address(cases[i].base,cases[i].offset)==cases[i].result);
   for(i=0;i<1000000;++i) {
      seed=seed*UINT32_C(1664525)+UINT32_C(1013904223);base=seed;
      seed=seed*UINT32_C(1664525)+UINT32_C(1013904223);encoded=seed&UINT32_C(0x7fffff);
      offset=(int32_t)encoded;
      if(encoded&UINT32_C(0x400000))offset-=8388608;
      wide=(uint64_t)base+UINT64_C(0x100000000)+(int64_t)offset;
      expected=(uint32_t)(wide%UINT64_C(0x100000000));
      CHECK(xe_vertex_fetch_address(base,offset)==expected);
   }
}

static void instructions(void)
{
   static const uint32_t indices[]={UINT32_C(0x4efffffe),UINT32_C(0x4f000000),UINT32_C(0x4f000001),UINT32_C(0x4f7fffff)};
   static const int32_t offsets[]={-4194304,-257,-1,0,1,256,4194303};
   uint32_t code[12],out[8];
   unsigned i,j,mini,lane;
   for(i=0;i<sizeof(indices)/sizeof(indices[0]);++i)
      for(j=0;j<sizeof(offsets)/sizeof(offsets[0]);++j)for(mini=0;mini<2;++mini) {
         memset(code,0,sizeof(code));
         /* EXEC_END of three instructions: full vfetch, mini vfetch, export.
          * Sequence tags are fetch, fetch, ALU. No control-flow speculation. */
         code[0]=1|(3u<<12)|(5u<<16);code[1]=2u<<12;
         code[3]=1u<<12;code[4]=(36u<<16)|(7u<<3)|(7u<<6)|(7u<<9);
         code[5]=1u|(((uint32_t)(mini?0:offsets[j])&UINT32_C(0x7fffff))<<8);
         code[6]=2u<<12;code[7]=code[4]|(1u<<30);
         code[8]=((uint32_t)(mini?offsets[j]:0)&UINT32_C(0x7fffff))<<8;
         /* max position, r1, r2; scalar retain; all vector lanes. */
         code[9]=62u|(1u<<15)|(15u<<16)|(50u<<26);
         code[11]=(1u<<16)|(2u<<8)|(2u<<24)|(1u<<31)|(1u<<30);
         memset(out,0x6d,sizeof(out));
         CHECK(xe_vertex_address_native(code,indices[i],out)==1);
         /* Empty fetch buffer: no guest pointer dereference is permitted. */
         for(lane=0;lane<4;++lane)CHECK(out[lane]==0);
         for(lane=4;lane<8;++lane)CHECK(out[lane]==UINT32_C(0x6d6d6d6d));
      }
}
int main(void)
{
   addresses();instructions();
   puts("PASS: 1000010 vertex address goldens; 56 full/mini packed interpreter cases and output guards");
   return 0;
}
