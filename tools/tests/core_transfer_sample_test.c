/* Independent integer oracle for the generic transfer emitter. BSD; C89. */
#include "libretro/core_transfer_sample.h"
#include <stdio.h>
#include <string.h>

static unsigned failure_points;
typedef struct machine {
 uint32_t values[512];unsigned count,attempts,fail_at;
} machine;
static uintptr_t emit(void *context,enum xe_transfer_sample_op op,
 uintptr_t a,uintptr_t b,uintptr_t c) {
 machine *m=(machine*)context;uint32_t x,y,v;
 ++m->attempts;
 if(m->attempts==m->fail_at || m->count+1>=512)return 0;
 if(op==XE_TRANSFER_SAMPLE_CONST)v=(uint32_t)a;
 else {
  if(!a || !b || a>m->count || b>m->count)return 0;
  x=m->values[a];y=m->values[b];
  switch(op) {
   case XE_TRANSFER_SAMPLE_MUL_HIGH:v=(uint32_t)(((uint64_t)x*y)>>32);break;
   case XE_TRANSFER_SAMPLE_EQ:v=x==y;break;
   case XE_TRANSFER_SAMPLE_MUL:v=x*y;break;
   case XE_TRANSFER_SAMPLE_SUB:v=x-y;break;
   case XE_TRANSFER_SAMPLE_GE:v=x>=y;break;
   case XE_TRANSFER_SAMPLE_ADD:v=x+y;break;
   case XE_TRANSFER_SAMPLE_SELECT:
    if(!c || c>m->count)return 0;
    v=x?y:m->values[c];break;
   case XE_TRANSFER_SAMPLE_AND:v=x&y;break;
   case XE_TRANSFER_SAMPLE_OR:v=x|y;break;
   case XE_TRANSFER_SAMPLE_XOR:v=x^y;break;
   case XE_TRANSFER_SAMPLE_SHR:if(y>31)return 0;v=x>>y;break;
   case XE_TRANSFER_SAMPLE_SHL:if(y>31)return 0;v=x<<y;break;
   default:return 0;
  }
 }
 m->values[++m->count]=v;return m->count;
}
static uintptr_t literal(machine *m,uint32_t v) {
 return emit(m,XE_TRANSFER_SAMPLE_CONST,v,0,0);
}
static uint32_t random_word(uint32_t *seed) {
 *seed=*seed*1664525u+1013904223u;return *seed;
}
static int test_case(uint32_t px,uint32_t py,unsigned sample,unsigned msaa,
 unsigned native,unsigned sx,unsigned sy,unsigned fail_at,unsigned fail_phase) {
 machine m={0};xe_transfer_sample_emitter e;
 xe_transfer_sample_parameters p;xe_transfer_sample_coordinates in,canonical,out,sentinel;
 uintptr_t x,y,s;uint32_t gx,gy,u,v,gs;xe_transfer_sample_config config;
 unsigned operations;
 e.emit=emit;e.context=&m;
 if(!xe_transfer_sample_prepare(&config,msaa,native,sx,sy))return 0;
 p.msaa=literal(&m,msaa);p.native2=literal(&m,native);
 p.scale_x=literal(&m,sx);p.scale_y=literal(&m,sy);
 p.reciprocal_x=literal(&m,config.reciprocal_x);p.reciprocal_y=literal(&m,config.reciprocal_y);
 x=literal(&m,px);y=literal(&m,py);s=literal(&m,sample);
 memset(&sentinel,0xa5,sizeof(sentinel));canonical=sentinel;
 m.attempts=0;m.fail_at=fail_phase==1?fail_at:0;
 if(!xe_transfer_sample_canonical_emit(&e,&p,x,y,s,&canonical))
  return ++failure_points && fail_phase==1 && fail_at && !memcmp(&canonical,&sentinel,sizeof(sentinel));
 operations=m.attempts;
 if(fail_phase==1 && fail_at && fail_at<=operations)return 0;
 gx=px/sx;gy=py/sy;
 if(msaa==2) {
  u=(gx&1)|((sample&1)<<1)|((gx>>1)<<2);
  v=(gy&1)|(sample&2)|((gy>>1)<<2);
 } else if(msaa==1) {
  gs=native?(sample^1):(sample>>1);
  u=(gx&0xfffffffdu)|((gs&1)<<1);
  v=(gy&1)|(gx&2)|((gy>>1)<<2);
 } else {u=gx;v=gy;}
 if(m.values[canonical.x]!=u || m.values[canonical.y]!=v ||
    m.values[canonical.sub_x]!=px%sx || m.values[canonical.sub_y]!=py%sy)return 0;
 /* Independently build inverse input, rather than trusting canonical output. */
 m.fail_at=0;
 in.x=literal(&m,u);in.y=literal(&m,v);in.sample=literal(&m,0);
 in.sub_x=literal(&m,px%sx);in.sub_y=literal(&m,py%sy);
 out=sentinel;m.attempts=0;m.fail_at=fail_phase==2?fail_at:0;
 if(!xe_transfer_sample_decanonical_emit(&e,&p,&in,&out))
  return ++failure_points && fail_phase==2 && fail_at && !memcmp(&out,&sentinel,sizeof(sentinel));
 if(msaa==2) {
  gx=(u&1)|((u>>2)<<1);gy=(v&1)|((v>>2)<<1);gs=((u>>1)&1)|(v&2);
 } else if(msaa==1) {
  gx=(u&0xfffffffdu)|(v&2);gy=(v&1)|((v>>2)<<1);
  gs=(u>>1)&1;gs=native?(gs^1):gs*3;
 } else {gx=u;gy=v;gs=0;}
 return m.values[out.x]==gx*sx+px%sx && m.values[out.y]==gy*sy+py%sy &&
        m.values[out.sample]==gs;
}
int main(void) {
 uint32_t seed=0x3871a55du;unsigned sx,sy,msaa,native,i,sample,fail;
 xe_transfer_sample_emitter empty={0};
 xe_transfer_sample_parameters parameters={0};
 xe_transfer_sample_coordinates coordinates={0},out;
 xe_transfer_sample_config config,saved;
 static const uint32_t edges[]={0,1,2,3,31,32,8191,0x7fffffffu,0x80000000u,0xffffffffu};
 unsigned count=0;
 memset(&config,0xa5,sizeof(config));saved=config;
 if(xe_transfer_sample_prepare(NULL,0,0,1,1) ||
    xe_transfer_sample_prepare(&config,3,0,1,1) ||
    xe_transfer_sample_prepare(&config,0,2,1,1) ||
    xe_transfer_sample_prepare(&config,0,0,0,1) ||
    xe_transfer_sample_prepare(&config,0,0,1,0) || memcmp(&config,&saved,sizeof(config)))return 1;
 memset(&out,0xa5,sizeof(out));coordinates=out;
 if(xe_transfer_sample_canonical_emit(NULL,&parameters,1,1,1,&out) ||
    xe_transfer_sample_canonical_emit(&empty,&parameters,1,1,1,&out) ||
    xe_transfer_sample_decanonical_emit(NULL,&parameters,&coordinates,&out) ||
    memcmp(&out,&coordinates,sizeof(out)))return 1;
 for(msaa=0;msaa<3;++msaa)for(native=0;native<2;++native)
  for(sx=1;sx<=16;++sx)for(sy=1;sy<=16;++sy)for(i=0;i<32;++i) {
   sample=msaa==2?(i&3):msaa==1?(native?(i&1):(i&1)*3):0;
   if(!test_case(i<10?edges[i]:random_word(&seed),i<10?edges[9-i]:random_word(&seed),
        sample,msaa,native,sx,sy,0,0)) {
    fprintf(stderr,"Sample mismatch msaa%u native%u scale%u,%u case%u\n",msaa,native,sx,sy,i);return 1;
   }
   ++count;
  }
 for(fail=1;fail<100;++fail)for(i=1;i<=2;++i)
  if(!test_case(271,973,3,1,0,3,7,fail,i))return 1;
 printf("PASS: %u scale/MSAA sample address cases and%u failure points\n",count,failure_points);
 return 0;
}
