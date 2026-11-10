/* Independent legacy-route oracle, including scale-before-canonicalize.
 * Original route: edram_transfer_shader.cc, Xenia authors. BSD; C89. */
#include "libretro/core_transfer_address.h"
#include <stdio.h>
#include <string.h>
#include "transfer_address_oracle.h"
typedef struct machine {uint32_t value[512];unsigned count,attempts,fail_at;} machine;
static uintptr_t emit(void *opaque,enum xe_transfer_sample_op op,uintptr_t a,uintptr_t b,uintptr_t c) {
 machine *m=(machine*)opaque;uint32_t v,x,y;
 ++m->attempts;if(m->attempts==m->fail_at || m->count>=511)return 0;
 if(op==XE_TRANSFER_SAMPLE_CONST)v=(uint32_t)a;
 else {
  if(!a || !b || a>m->count || b>m->count)return 0;
  x=m->value[a];y=m->value[b];
  switch(op) {
   case XE_TRANSFER_SAMPLE_MUL_HIGH:v=(uint32_t)(((uint64_t)x*y)>>32);break;
   case XE_TRANSFER_SAMPLE_EQ:v=x==y;break;
   case XE_TRANSFER_SAMPLE_GE:v=x>=y;break;
   case XE_TRANSFER_SAMPLE_ADD:v=x+y;break;
   case XE_TRANSFER_SAMPLE_SUB:v=x-y;break;
   case XE_TRANSFER_SAMPLE_MUL:v=x*y;break;
   case XE_TRANSFER_SAMPLE_AND:v=x&y;break;
   case XE_TRANSFER_SAMPLE_OR:v=x|y;break;
   case XE_TRANSFER_SAMPLE_XOR:v=x^y;break;
   case XE_TRANSFER_SAMPLE_SHL:if(y>31)return 0;v=x<<y;break;
   case XE_TRANSFER_SAMPLE_SHR:if(y>31)return 0;v=x>>y;break;
   case XE_TRANSFER_SAMPLE_SELECT:if(!c || c>m->count)return 0;v=x?y:m->value[c];break;
   default:return 0;
  }
 }
 m->value[++m->count]=v;return m->count;
}
static uintptr_t literal(machine *m,uint32_t v){return emit(m,XE_TRANSFER_SAMPLE_CONST,v,0,0);}
static unsigned failure_points;
static int check(const xe_transfer_address_key *key,uint32_t x,uint32_t y,uint32_t sample,unsigned fail_at) {
 machine m={0};xe_transfer_address_config config;xe_transfer_address_parameters params;
 xe_transfer_sample_emitter e;xe_transfer_address_result result,saved;uint32_t expected[10];
 uintptr_t ix,iy,is;unsigned i;
 if(!xe_transfer_address_prepare(&config,key))return 0;
 e.emit=emit;e.context=&m;
 for(i=0;i<XE_ADDR_WORDS;++i)params.word[i]=literal(&m,config.word[i]);
 ix=literal(&m,x);iy=literal(&m,y);is=literal(&m,sample);
 memset(&result,0xa5,sizeof(result));saved=result;m.attempts=0;m.fail_at=fail_at;
 if(!xe_transfer_address_emit(&e,&params,ix,iy,is,&result)) {
  ++failure_points;return fail_at && !memcmp(&result,&saved,sizeof(result));
 }
 legacy(key,x,y,sample,expected);
#define MATCH(field,n) if(m.value[result.field]!=expected[n]) { \
 fprintf(stderr,"Address %s: %08x vs%08x Dms%u Sms%u Dw%u Sw%u scales%u,%u->%u,%u pixel%u,%u sample%u\n", \
 #field,m.value[result.field],expected[n],key->dest_msaa,key->source_msaa,key->dest_wide,key->source_wide, \
 key->dest_scale_x,key->dest_scale_y,key->source_scale_x,key->source_scale_y,x,y,sample);return 0; }
 MATCH(x,0);MATCH(y,1);MATCH(sample,2);MATCH(second_x,3);MATCH(half,4);MATCH(two_loads,5);MATCH(tile,6);
 MATCH(dest_tile,7);MATCH(dest_x,8);MATCH(dest_y,9);
#undef MATCH
 return 1;
}
int main(void) {
 static const unsigned scales[6][4]={{1,1,1,1},{2,2,2,2},{3,5,3,5},{1,1,3,5},{3,5,1,1},{1,1,1,3}};
 static const uint32_t edges[]={0,1,19,20,39,40,79,80,1279,1280,2047,4095,0x80000000u,0xffffffffu};
 static const int32_t deltas[]={-2047,-65,-1,0,1,63,2047};
 xe_transfer_address_key k={0},valid;xe_transfer_address_config bad,saved;
 unsigned dm,sm,dw,sw,dc,sc,native,scale,i,sample,tests=0;uint32_t seed=0x6148fcu,x,y;
 k.dest_scale_x=k.dest_scale_y=k.source_scale_x=k.source_scale_y=1;
 k.dest_pitch=k.source_pitch=1;k.dest_color=k.source_color=1;
 valid=k;
 memset(&bad,0xa5,sizeof(bad));saved=bad;
 if(xe_transfer_address_prepare(NULL,&k) || xe_transfer_address_prepare(&bad,NULL) ||
    memcmp(&bad,&saved,sizeof(bad)))return 1;
#define INVALID(field,value) do { k=valid;k.field=value; \
 if(xe_transfer_address_prepare(&bad,&k) || memcmp(&bad,&saved,sizeof(bad)))return 1; } while(0)
 INVALID(source_pitch,0);INVALID(dest_pitch,0);INVALID(source_pitch,1024);
 INVALID(dest_pitch,1024);INVALID(source_scale_x,UINT32_MAX);INVALID(dest_scale_y,UINT32_MAX);
 INVALID(source_scale_y,0);INVALID(dest_scale_x,0);INVALID(dest_msaa,3);INVALID(source_msaa,3);
 INVALID(dest_native2,2);INVALID(source_native2,1);INVALID(dest_color,2);INVALID(source_wide,2);
 INVALID(dest_to_source_base,-2048);INVALID(dest_to_source_base,2048);
#undef INVALID
 k=valid;k.dest_color=0;k.dest_wide=1;
 if(xe_transfer_address_prepare(&bad,&k) || memcmp(&bad,&saved,sizeof(bad)))return 1;
 k=valid;k.dest_scale_x=2;k.dest_scale_y=3;k.source_scale_x=3;k.source_scale_y=2;
 if(xe_transfer_address_prepare(&bad,&k) || memcmp(&bad,&saved,sizeof(bad)))return 1;
 for(dm=0;dm<3;++dm)for(sm=0;sm<3;++sm)for(dw=0;dw<2;++dw)for(sw=0;sw<2;++sw)
 for(dc=0;dc<2;++dc)for(sc=0;sc<2;++sc)for(native=0;native<2;++native)for(scale=0;scale<6;++scale) {
  if((dw && !dc) || (sw && !sc))continue;
  k.dest_msaa=dm;k.source_msaa=sm;k.dest_wide=dw;k.source_wide=sw;k.dest_color=dc;k.source_color=sc;
  k.dest_native2=k.source_native2=native;
  k.dest_scale_x=scales[scale][0];k.dest_scale_y=scales[scale][1];
  k.source_scale_x=scales[scale][2];k.source_scale_y=scales[scale][3];
  for(i=0;i<64;++i) {
   seed=seed*1664525u+1013904223u;x=i<14?edges[i]:seed;
   seed=seed*1664525u+1013904223u;y=i<14?edges[13-i]:seed;
   k.dest_pitch=i%31+1;k.source_pitch=i%2?i%63+1:1023;k.dest_to_source_base=deltas[i%7];
   sample=dm==2?(i&3):dm==1?(native?(i&1):(i&1)*3):0;
   if(!check(&k,x,y,sample,0))return 1;
   ++tests;
  }
 }
 for(i=1;i<256;++i)if(!check(&k,271,873,3,i))return 1;
 printf("PASS: %u physical address cases and%u failure points\n",tests,failure_points);return 0;
}
