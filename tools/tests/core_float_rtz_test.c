/* Independent x86 SSE reference: rounding + DAZ/FTZ only in this test process. */
#include "libretro/core_float_rtz.h"
#include "libretro/core_float_rtz_emit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_M_X64) || defined(__x86_64__) || defined(__SSE__)
#include <xmmintrin.h>
#define XE_RTZ_SSE 1
#endif
#define CHECK(x) do {if(!(x)){fprintf(stderr,"rtz line %u\n",(unsigned)__LINE__);abort();}}while(0)

typedef struct Eval {uint32_t values[512];unsigned count,fail_at,failures;} Eval;
static xe_rtz_id evaluate(void *opaque,unsigned op,xe_rtz_id a,xe_rtz_id b,xe_rtz_id c,uint32_t literal) {
   Eval *v=(Eval *)opaque;
   uint32_t x=v->values[a],y=v->values[b],out=0;
   CHECK(v->count<511);
   if(v->fail_at && v->count==v->fail_at){++v->failures;return 0;}
   switch(op) {
   case XE_RTZ_CONST:out=literal;break;
   case XE_RTZ_ADD:out=x+y;break;case XE_RTZ_SUB:out=x-y;break;case XE_RTZ_MUL:out=x*y;break;
   case XE_RTZ_AND:out=x&y;break;case XE_RTZ_OR:out=x|y;break;case XE_RTZ_XOR:out=x^y;break;
   case XE_RTZ_SHL:CHECK(y<32);out=x<<y;break;case XE_RTZ_SHR:CHECK(y<32);out=x>>y;break;
   case XE_RTZ_EQ:out=x==y;break;case XE_RTZ_ULT:out=x<y;break;case XE_RTZ_ULE:out=x<=y;break;
   case XE_RTZ_UGE:out=x>=y;break;case XE_RTZ_BOOL_AND:out=x&&y;break;
   case XE_RTZ_MUL_TRUNC16:out=(uint32_t)(((uint64_t)x*y)>>16);break;
   case XE_RTZ_SELECT:out=x?y:v->values[c];break;case XE_RTZ_CLZ:out=xe_float_rtz_clz(x);break;
   default:abort();
   }
   v->values[++v->count]=out;return v->count;
}
static uint32_t graph(uint32_t a,uint32_t b,uint32_t native_result,unsigned multiply) {
   Eval v;xe_rtz_emitter emitter;xe_rtz_id result;
   memset(&v,0,sizeof(v));v.values[1]=a;v.values[2]=b;v.values[3]=native_result;v.count=3;
   emitter.context=&v;emitter.emit=evaluate;emitter.failed=0;
   result=multiply?xe_rtz_emit_mul(&emitter,1,2,3):xe_rtz_emit_add(&emitter,1,2,3);
   CHECK(result && !emitter.failed);
   return v.values[result];
}
static void faults(void) {
   Eval v;xe_rtz_emitter emitter;unsigned i,mul;
   for(mul=0;mul<2;++mul)for(i=4;i<160;++i) {
      xe_rtz_id result;
      memset(&v,0,sizeof(v));v.count=3;v.fail_at=i;
      emitter.context=&v;emitter.emit=evaluate;emitter.failed=0;
      result=mul?xe_rtz_emit_mul(&emitter,1,2,3):xe_rtz_emit_add(&emitter,1,2,3);
      if(v.failures)CHECK(!result && emitter.failed && v.failures==1 && v.count==i);else CHECK(result && !emitter.failed);
   }
}

static uint32_t rng=UINT32_C(0x4935967b);
static uint32_t random_bits(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static void classified_flush_tests(const uint32_t *edges,unsigned edge_count) {
   Eval v;xe_rtz_emitter emitter;xe_rtz_id result,predicate;
   uint32_t bits;unsigned i;
   for(i=0;i<65536;++i) {
      bits=random_bits();
      if(i<edge_count)bits=edges[i];
      else if(i<edge_count+512)bits=(((i-edge_count)&255)<<23) |
         ((i-edge_count)&256?UINT32_C(0x80000000):0);
      memset(&v,0,sizeof(v));v.count=1;v.values[1]=bits;
      emitter.context=&v;emitter.emit=evaluate;emitter.failed=0;
      result=xe_rtz_emit_flush_classified(&emitter,1,&predicate);
      CHECK(result && predicate && !emitter.failed);
      CHECK(v.values[predicate]==(uint32_t)!(bits&UINT32_C(0x7f800000)));
      CHECK(v.values[result]==((bits&UINT32_C(0x7f800000))?bits:bits&UINT32_C(0x80000000)));
   }
   for(i=1;i<12;++i) {
      memset(&v,0,sizeof(v));v.count=1;v.fail_at=i;
      emitter.context=&v;emitter.emit=evaluate;emitter.failed=0;predicate=99;
      result=xe_rtz_emit_flush_classified(&emitter,1,&predicate);
      if(v.failures)CHECK(!result && !predicate && emitter.failed && v.failures==1);
      else CHECK(result && predicate && !emitter.failed);
   }
   emitter.failed=0;CHECK(!xe_rtz_emit_flush_classified(&emitter,1,NULL) && emitter.failed);
   predicate=99;CHECK(!xe_rtz_emit_flush_classified(NULL,1,&predicate) && !predicate);
   emitter.failed=0;predicate=99;
   CHECK(!xe_rtz_emit_flush_classified(&emitter,0,&predicate) && !predicate && emitter.failed);
   puts("PASS: classified FTZ bits/predicate,65536 patterns, exponent/sign coverage and injected emission failures");
}
#ifdef XE_RTZ_SSE
#if defined(_MSC_VER)
#define NOINLINE __declspec(noinline)
#else
#define NOINLINE __attribute__((noinline))
#endif
static NOINLINE uint32_t reference(uint32_t a,uint32_t b,int multiply) {
   float fa,fb,out;uint32_t result;__m128 av,bv,value;
   memcpy(&fa,&a,4);memcpy(&fb,&b,4);
   av=_mm_set_ss(fa);bv=_mm_set_ss(fb);
   value=multiply?_mm_mul_ss(av,bv):_mm_add_ss(av,bv);
   _mm_store_ss(&out,value);memcpy(&result,&out,4);return result;
}
static void check_pair(uint32_t a,uint32_t b) {
   uint32_t expected,actual;unsigned mul;
   float fa,fb,fr;uint32_t wrapped;
   for(mul=0;mul<2;++mul) {
      expected=reference(a,b,(int)mul);
      actual=mul?xe_float_mul_rtz_bits(a,b,expected):xe_float_add_rtz_bits(a,b,expected);
      CHECK(graph(a,b,expected,mul)==expected);
      memcpy(&fa,&a,4);memcpy(&fb,&b,4);
      _mm_setcsr((_mm_getcsr()&~0x6000u));
      fr=mul?xe_float_mul_rtz(fa,fb):xe_float_add_rtz(fa,fb);
      memcpy(&wrapped,&fr,4);
      if((expected&UINT32_C(0x7fffffff))>UINT32_C(0x7f800000))CHECK((wrapped&UINT32_C(0x7fffffff))>UINT32_C(0x7f800000));
      else if(wrapped!=expected){fprintf(stderr,"wrapper %u %08x %08x expected %08x got %08x\n",mul,(unsigned)a,(unsigned)b,(unsigned)expected,(unsigned)wrapped);abort();}
      _mm_setcsr(_mm_getcsr()|0x6000u);
      if(actual!=expected){fprintf(stderr,"%s %08x %08x expected %08x actual %08x\n",mul?"mul":"add",(unsigned)a,(unsigned)b,(unsigned)expected,(unsigned)actual);abort();}
   }
}
#endif
int main(void) {
   static const uint32_t edges[]={0,UINT32_C(0x80000000),1,UINT32_C(0x80000001),
      UINT32_C(0x007fffff),UINT32_C(0x807fffff),UINT32_C(0x00800000),UINT32_C(0x80800000),
      UINT32_C(0x00800001),UINT32_C(0x80800001),UINT32_C(0x3f800000),UINT32_C(0xbf800000),
      UINT32_C(0x3f800001),UINT32_C(0xbf800001),UINT32_C(0x7f7fffff),UINT32_C(0xff7fffff),
      UINT32_C(0x7f800000),UINT32_C(0xff800000),UINT32_C(0x7fc12345),UINT32_C(0xff812345)};
   unsigned i,j;
#ifdef XE_RTZ_SSE
   unsigned control=_mm_getcsr();
   /* Mask exceptions, FTZ/DAZ on, round toward zero. Restore before exit. */
   _mm_setcsr((control&~0x603fu)|0xffc0u);
   for(i=0;i<sizeof(edges)/sizeof(edges[0]);++i)for(j=0;j<sizeof(edges)/sizeof(edges[0]);++j)check_pair(edges[i],edges[j]);
   for(i=0;i<2000000;++i)check_pair(random_bits(),random_bits());
   for(i=1;i<255;++i)for(j=0;j<128;++j) {
      uint32_t a=(i<<23)|(random_bits()&UINT32_C(0x7fffff));
      uint32_t b=(a^UINT32_C(0x80000000))+(j&63);
      check_pair(a,b);check_pair(a^UINT32_C(0x80000000),b^UINT32_C(0x80000000));
   }
   _mm_setcsr(control);
   puts("PASS: integer RTZ add/mul vs SSE RTZ+FTZ/DAZ, 4M random operations and cancellation/edge cases");
#else
   (void)edges;(void)i;(void)j;(void)random_bits;
#endif
   for(i=0;i<sizeof(edges)/sizeof(edges[0]);++i) {
      Eval v;xe_rtz_emitter emitter;xe_rtz_id result;
      memset(&v,0,sizeof(v));v.count=1;v.values[1]=edges[i];
      emitter.context=&v;emitter.emit=evaluate;emitter.failed=0;
      result=xe_rtz_emit_flush(&emitter,1);
      CHECK(result && !emitter.failed);
      CHECK(v.values[result]==((edges[i]&UINT32_C(0x7f800000))?edges[i]:(edges[i]&UINT32_C(0x80000000))));
   }
   faults();
   classified_flush_tests(edges,(unsigned)(sizeof(edges)/sizeof(edges[0])));
   CHECK(xe_float_add_rtz_bits(UINT32_C(0x3f800000),UINT32_C(0x33c00000),0)==UINT32_C(0x3f800000));
   CHECK(xe_float_add_rtz_bits(UINT32_C(0x3f800000),UINT32_C(0x80800000),0)==UINT32_C(0x3f7fffff));
   CHECK(xe_float_mul_rtz_bits(UINT32_C(0x7f7fffff),UINT32_C(0x40000000),0)==UINT32_C(0x7f7fffff));
   CHECK(graph(UINT32_C(0x3f800000),UINT32_C(0x33c00000),0,0)==UINT32_C(0x3f800000));
   puts("PASS: SSA recipe arithmetic/shift safety, injected emission failures and native special-value forwarding");
   puts("PASS: RTZ non-tie boundary, tiny opposite-sign add and finite overflow golden values");
   return 0;
}
