/* Typed CPU execution of portable raw-IR format recipes. BSD, MSVC C89.
 * Literal goldens are shared facts with the existing native DXIL fixture.
 * An independent scalar model checks all 1024 7e3 values. No heap allocation. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "libretro/core_transfer_format.h"
#include "transfer_small_float_reference.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"transfer format line %u\n",(unsigned)__LINE__); exit(1); } } while (0)
enum { U32, F32, TF_BOOLEAN };
typedef struct value { uint32_t bits; unsigned type; } value;
typedef struct machine { value values[4096]; unsigned count, fail_at; } machine;
static machine vm;
static float floating(uint32_t bits) { float f; memcpy(&f,&bits,4); return f; }
static uint32_t bits(float f) { uint32_t u; memcpy(&u,&f,4); return u; }
static uintptr_t push(machine *m,unsigned type,uint32_t b)
{
   if (m->fail_at && m->count+1 == m->fail_at) return 0;
   CHECK(m->count+1 < sizeof(m->values)/sizeof(m->values[0]));
   ++m->count; m->values[m->count].type=type; m->values[m->count].bits=b;
   return m->count;
}
static value get(machine *m,uintptr_t id,unsigned type)
{
   CHECK(id && id<=m->count && m->values[id].type==type);
   return m->values[id];
}
static uintptr_t emit(void *opaque,enum xe_transfer_format_op op,uintptr_t a,uintptr_t b,uintptr_t c)
{
   machine *m=(machine*)opaque;
   value av,bv,cv;
   float af=0,bf=0,f=0;
   uint32_t u=0;
   unsigned type=U32;
   if (op==XE_TF_U32 || op==XE_TF_F32_BITS)
      return push(m,op==XE_TF_U32?U32:F32,(uint32_t)a);
   if (op==XE_TF_SELECT_U32 || op==XE_TF_SELECT_F32) {
      type=op==XE_TF_SELECT_U32?U32:F32;
      av=get(m,a,TF_BOOLEAN); bv=get(m,b,type); cv=get(m,c,type);
      return push(m,type,av.bits?bv.bits:cv.bits);
   }
   switch(op) {
      case XE_TF_ADD_U32:case XE_TF_SUB_U32:case XE_TF_AND_U32:case XE_TF_OR_U32:
      case XE_TF_SHL_U32:case XE_TF_SHR_U32:case XE_TF_EQ_U32:case XE_TF_NE_U32:
      case XE_TF_LT_U32:case XE_TF_GE_U32:
         av=get(m,a,U32);bv=get(m,b,U32);break;
      case XE_TF_U32_TO_F32:case XE_TF_BITCAST_U32_F32:
         av=get(m,a,U32); bv=av; break;
      default:
         av=get(m,a,F32); bv=av;
         if(op==XE_TF_ADD_F32 || op==XE_TF_MUL_F32 || op==XE_TF_LT_F32 ||
               op==XE_TF_GT_F32 || op==XE_TF_GE_F32 || op==XE_TF_CLAMP_POSITIVE_F32) bv=get(m,b,F32);
         af=floating(av.bits);bf=floating(bv.bits);break;
   }
   switch(op) {
      case XE_TF_ADD_U32:u=av.bits+bv.bits;break;
      case XE_TF_SUB_U32:u=av.bits-bv.bits;break;
      case XE_TF_AND_U32:u=av.bits&bv.bits;break;
      case XE_TF_OR_U32:u=av.bits|bv.bits;break;
      case XE_TF_SHL_U32:CHECK(bv.bits<32);u=av.bits<<bv.bits;break;
      case XE_TF_SHR_U32:CHECK(bv.bits<32);u=av.bits>>bv.bits;break;
      case XE_TF_EQ_U32:type=TF_BOOLEAN;u=av.bits==bv.bits;break;
      case XE_TF_NE_U32:type=TF_BOOLEAN;u=av.bits!=bv.bits;break;
      case XE_TF_LT_U32:type=TF_BOOLEAN;u=av.bits<bv.bits;break;
      case XE_TF_GE_U32:type=TF_BOOLEAN;u=av.bits>=bv.bits;break;
      case XE_TF_ADD_F32:type=F32;f=af+bf;u=bits(f);break;
      case XE_TF_MUL_F32:type=F32;f=af*bf;u=bits(f);break;
      case XE_TF_LT_F32:type=TF_BOOLEAN;u=af<bf;break;
      case XE_TF_GT_F32:type=TF_BOOLEAN;u=af>bf;break;
      case XE_TF_GE_F32:type=TF_BOOLEAN;u=af>=bf;break;
      case XE_TF_U32_TO_F32:type=F32;u=bits((float)av.bits);break;
      case XE_TF_F32_TO_U32:CHECK(af>=0 && (double)af<4294967296.0);u=(uint32_t)af;break;
      case XE_TF_BITCAST_U32_F32:type=F32;u=av.bits;break;
      case XE_TF_BITCAST_F32_U32:u=av.bits;break;
      case XE_TF_TRUNC_F32:type=F32;u=bits((float)(af<0?ceil((double)af):floor((double)af)));break;
      case XE_TF_ROUND_EVEN_F32:
         {double lo=floor((double)af),fraction=(double)af-lo;
          if(fraction>.5 || (fraction==.5 && fmod(lo,2.0)!=0))lo+=1;
          type=F32;u=bits((float)lo);if(!lo && (av.bits&UINT32_C(0x80000000)))u|=UINT32_C(0x80000000);}
         break;
      case XE_TF_SATURATE_F32:type=F32;u=!(af>0)?0:af<1?av.bits:UINT32_C(0x3f800000);break;
      case XE_TF_CLAMP_POSITIVE_F32:type=F32;CHECK(bf>0 && bf<=3.402823466e38f);u=!(af>0)?0:af<bf?av.bits:bv.bits;break;
      default:CHECK(0);break;
   }
   return push(m,type,u);
}
static xe_transfer_format_emitter open_machine(unsigned fail_at)
{
   xe_transfer_format_emitter e;
   vm.count=0;vm.fail_at=fail_at;e.emit=emit;e.context=&vm;e.failed=0;return e;
}
static const uint32_t small[][2]={
   {0,0},{0x80000000,0},{0x3b000000,1},{0x3e7e0000,127},{0x3e800000,128},
   {0x3f000000,256},{0x3f800000,384},{0x41ff0000,1023},{0x7f800000,1023},
   {0xff800000,0},{0x7fc12345,0},{0x3f808000,384},{0x3f818000,386}
};
static const uint32_t depths[][6]={
   {0,0,0,0,0,0},{0x2e800000,1,1,0x2e800000,0x2e800000,0},
   {0x2e000000,0,0,0,0,0},{0x2e000001,0,0,0,0,0},
   {0x2e200000,0,1,0,0x2e800000,0},
   {0x38800000,0x100000,0x100000,0x38800000,0x38800000,0},
   {0x3f000000,0xe00000,0xe00000,0x3f000000,0x3f000000,0},
   {0x3f000004,0xe00000,0xe00000,0x3f000000,0x3f000000,0},
   {0x3f00000c,0xe00001,0xe00002,0x3f000008,0x3f000010,0},
   {0x3ffffff8,0xffffff,0xffffff,0x3ffffff8,0x3ffffff8,0},
   {0x2e000000,1,1,0x2e000000,0x2e000000,1},
   {0x38000000,0x100000,0x100000,0x38000000,0x38000000,1},
   {0x3f000000,0xf00000,0xf00000,0x3f000000,0x3f000000,1},
   {0x3f7ffff8,0xffffff,0xffffff,0x3f7ffff8,0x3f7ffff8,1}
};
static void small_depth(void)
{
   xe_transfer_format_emitter e;
   uintptr_t source,packed,decoded;
   uint32_t exponent,mantissa,reference;
   unsigned i,j;
   for(i=0;i<sizeof(small)/sizeof(small[0]);++i){
      e=open_machine(0);source=xe_tf_f(&e,floating(small[i][0]));
      packed=xe_transfer_format_pack7e3(&e,source);
      CHECK(packed && get(&vm,packed,U32).bits==small[i][1]);
   }
   for(i=0;i<1024;++i){
      e=open_machine(0);source=xe_tf_u(&e,i);
      decoded=xe_transfer_format_unpack7e3(&e,source);
      exponent=i>>7;mantissa=i&127;
      reference=bits(exponent?(float)ldexp((double)(128+mantissa),(int)exponent-10):(float)mantissa/512.0f);
      CHECK(decoded && get(&vm,decoded,F32).bits==reference);
      packed=xe_transfer_format_pack7e3(&e,decoded);
      CHECK(packed && get(&vm,packed,U32).bits==i);
   }
   for(i=0;i<sizeof(depths)/sizeof(depths[0]);++i)for(j=0;j<2;++j){
      e=open_machine(0);source=xe_tf_f(&e,floating(depths[i][0]));
      packed=xe_transfer_format_pack20e4(&e,source,j,depths[i][5]);
      CHECK(packed && get(&vm,packed,U32).bits==depths[i][1+j]);
      decoded=xe_transfer_format_unpack20e4(&e,packed,depths[i][5]);
      CHECK(decoded && get(&vm,decoded,F32).bits==depths[i][3+j]);
   }
}
static void exhaustive_small_rounding(void){
 xe_transfer_format_emitter e;uintptr_t source,packed;uint32_t input;unsigned i;
 for(i=0;i<XE_TEST_7E3_ROUNDING_CASES;++i){
  input=ref_7e3_rounding_input(i);e=open_machine(0);
  source=xe_tf_f(&e,floating(input));packed=xe_transfer_format_pack7e3(&e,source);
  CHECK(packed && get(&vm,packed,U32).bits==ref_7e3_pack_bits(input));
 }
}
typedef struct color_case {
   unsigned format, integer_view, words;
   uint32_t source[4], packed[2];
} color_case;
static const color_case colors[]={
   {0,0,1,{0x3f800000,0x3f000000,0,0x3f800000},{0xff0080ff,0}},
   {1,0,1,{0x3f800000,0x3f000000,0,0x3f800000},{0xff00bfff,0}},
   {2,0,1,{0x3f800000,0x3f000000,0,0x3f800000},{0xc00803ff,0}},
   {3,0,1,{0x3f800000,0x3f000000,0,0x3f800000},{0xc0040180,0}},
   {4,1,1,{0x7e01,0x8000,0xffff,0xff80},{0x80007e01,0}},
   {5,1,2,{0x7e01,0x8000,0xffff,0xff80},{0x80007e01,0xff80ffff}},
   {6,1,1,{0x7e01,0x8000,0xffff,0xff80},{0x80007e01,0}},
   {7,1,2,{0x7e01,0x8000,0xffff,0xff80},{0x80007e01,0xff80ffff}},
   {10,0,1,{0x3f800000,0x3f000000,0,0x3f800000},{0xc00803ff,0}},
   {12,0,1,{0x3f800000,0x3f000000,0,0x3f800000},{0xc0040180,0}},
   {14,1,1,{0x7fc12345,0x80000000,0,0},{0x7fc12345,0}},
   {15,1,2,{0x7fc12345,0x80000000,0,0},{0x7fc12345,0x80000000}},
   {14,0,1,{0x7fc12345,0x80000000,0,0},{0x7fc12345,0}},
   {15,0,2,{0x7fc12345,0x80000000,0,0},{0x7fc12345,0x80000000}}
};
static void color_formats(void)
{
   xe_transfer_format_emitter e;
   uintptr_t source[4],packed[2],unpacked[4];
   unsigned i,j,n,ops,failure,components;
   uint32_t expected;
   for(i=0;i<sizeof(colors)/sizeof(colors[0]);++i){
      e=open_machine(0);
      for(j=0;j<4;++j)source[j]=colors[i].integer_view?
         xe_tf_u(&e,colors[i].source[j]):xe_tf_f(&e,floating(colors[i].source[j]));
      n=xe_transfer_format_pack_color(&e,colors[i].format,colors[i].integer_view,source,packed);
      CHECK(n==colors[i].words);
      for(j=0;j<2;++j)CHECK(get(&vm,packed[j],U32).bits==colors[i].packed[j]);
      ops=vm.count;
      if(colors[i].integer_view || colors[i].format==3 || colors[i].format==12 || colors[i].format>=14){
         CHECK(xe_transfer_format_unpack_color(&e,colors[i].format,colors[i].integer_view,packed,unpacked)==n);
         components=xe_transfer_format_components(colors[i].format);
         for(j=0;j<4;++j){
            expected=j<components?colors[i].source[j]:0;
            CHECK(get(&vm,unpacked[j],colors[i].integer_view?U32:F32).bits==expected);
         }
      }
      /* Every failed emit keeps caller-owned output IDs unpublished. */
      for(failure=1;failure<=ops;++failure){
         e=open_machine(failure);packed[0]=123;packed[1]=456;
         for(j=0;j<4;++j)source[j]=colors[i].integer_view?
            xe_tf_u(&e,colors[i].source[j]):xe_tf_f(&e,floating(colors[i].source[j]));
         CHECK(!xe_transfer_format_pack_color(&e,colors[i].format,colors[i].integer_view,source,packed));
         CHECK(packed[0]==123 && packed[1]==456);
      }
   }
}
static void gamma_roundtrip(void)
{
   xe_transfer_format_emitter e;
   uintptr_t decoded,encoded,source;
   unsigned byte,q16;
   float linear,normalized,gamma;
   for(byte=0;byte<256;++byte){
      e=open_machine(0);
      decoded=xe_tf_gamma_byte(&e,xe_tf_u(&e,byte));
      CHECK(decoded);linear=floating(get(&vm,decoded,F32).bits);
      CHECK(linear>=0 && linear<=1);
      q16=(unsigned)(linear*65535.0f+.5f);
      normalized=(float)q16/65535.0f;
      source=xe_tf_f(&e,normalized);
      encoded=xe_transfer_format_gamma_encode(&e,source);
      CHECK(encoded);gamma=floating(get(&vm,encoded,F32).bits);
      CHECK((unsigned)(gamma*255.0f+.5f)==byte);
      /* Independently expressed integer piecewise lower-edge reference. */
      q16=byte>=192?8:byte>=96?4:byte>=64?2:1;
      q16=byte*q16-(byte>=192?1024:byte>=96?256:byte>=64?64:0);
      normalized=(float)(q16+((q16*(byte>=192?8:byte>=96?4:byte>=64?2:1))>>10))*(1.0f/1023.0f);
      decoded=xe_transfer_format_gamma_decode(&e,xe_tf_f(&e,(float)byte/255.0f));
      CHECK(decoded && fabs((double)floating(get(&vm,decoded,F32).bits)-(double)normalized)<0.0000002);
   }
}
static void unpack_failures(void)
{
   xe_transfer_format_emitter e;
   uintptr_t packed[2],unpacked[4];
   unsigned i,j,n,ops,failure;
   for(i=0;i<sizeof(colors)/sizeof(colors[0]);++i){
      e=open_machine(0);
      for(j=0;j<2;++j)packed[j]=xe_tf_u(&e,colors[i].packed[j]);
      n=xe_transfer_format_unpack_color(&e,colors[i].format,colors[i].integer_view,packed,unpacked);
      CHECK(n==colors[i].words);ops=vm.count;
      for(failure=1;failure<=ops;++failure){
         e=open_machine(failure);
         for(j=0;j<4;++j)unpacked[j]=123+j;
         for(j=0;j<2;++j)packed[j]=xe_tf_u(&e,colors[i].packed[j]);
         CHECK(!xe_transfer_format_unpack_color(&e,colors[i].format,colors[i].integer_view,packed,unpacked));
         for(j=0;j<4;++j)CHECK(unpacked[j]==123+j);
      }
   }
}
static void direct_formats(void)
{
   xe_transfer_format_emitter e;
   uintptr_t source[4],out[4];unsigned i,j,b,scale,linear,ops,failure;
   float reference,actual;
   for(i=0;i<sizeof(colors)/sizeof(colors[0]);++i){
      e=open_machine(0);
      for(j=0;j<4;++j)source[j]=colors[i].integer_view?xe_tf_u(&e,colors[i].source[j]):
         xe_tf_op(&e,XE_TF_F32_BITS,colors[i].source[j],0,0);
      ops=vm.count;
      CHECK(xe_transfer_format_direct_color(&e,colors[i].format,colors[i].format,colors[i].integer_view,source,out));
      CHECK(vm.count==ops);
      for(j=0;j<4;++j)CHECK(out[j]==source[j]);
   }
   for(b=0;b<256;++b){
      e=open_machine(0);
      for(j=0;j<3;++j)source[j]=xe_tf_f(&e,(float)b/255.0f);
      source[3]=xe_tf_f(&e,.5f);
      CHECK(xe_transfer_format_direct_color(&e,0,1,0,source,out));
      CHECK(out[3]==source[3]);
      scale=b>=192?8:b>=96?4:b>=64?2:1;
      linear=b*scale-(b>=192?1024:b>=96?256:b>=64?64:0);
      reference=(float)(linear+((linear*scale)>>10))*(1.0f/1023.0f);
      actual=floating(get(&vm,out[0],F32).bits);
      CHECK(fabs((double)actual-reference)<0.0000002);
      /* In particular, byte255 maps to1, not the raw codec's midpoint. */
      if(b==255)CHECK(get(&vm,out[0],F32).bits==UINT32_C(0x3f800000));
   }
   e=open_machine(0);for(j=0;j<4;++j)source[j]=xe_tf_f(&e,.5f);
   CHECK(xe_transfer_format_direct_color(&e,1,0,0,source,out));ops=vm.count;
   CHECK(out[3]==source[3]);
   for(failure=1;failure<=ops;++failure){
      e=open_machine(failure);
      for(j=0;j<4;++j){source[j]=xe_tf_f(&e,.5f);out[j]=123+j;}
      CHECK(!xe_transfer_format_direct_color(&e,1,0,0,source,out));
      for(j=0;j<4;++j)CHECK(out[j]==123+j);
   }
   e=open_machine(0);for(j=0;j<4;++j){source[j]=xe_tf_u(&e,j);out[j]=123+j;}
   CHECK(xe_transfer_format_direct_color(&e,4,6,1,source,out));
   for(j=0;j<4;++j)CHECK(out[j]==source[j]);
   CHECK(!xe_transfer_format_direct_color(&e,14,15,1,source,out));
   CHECK(!xe_transfer_format_direct_color(&e,4,4,0,source,out));
}
int main(void)
{
   small_depth();exhaustive_small_rounding();color_formats();gamma_roundtrip();unpack_failures();direct_formats();
   puts("PASS: typed portable EDRAM IR, 14 literal color/view cases with every pack/unpack emit failure, borrowed identity IDs and direct gamma lower edges, 7e3/20e4 goldens, 1024 independent small-float roundtrips, 65536 half encodings and 3069 midpoint neighbors against an independent nearest-value oracle, 256 gamma roundtrips; image/renderer coverage is separate");
   return 0;
}
