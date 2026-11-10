/* Runtime physical-address records and independent legacy oracle. BSD; C89. */
#ifndef XE_TEST_TRANSFER_ADDRESS_CASES_H
#define XE_TEST_TRANSFER_ADDRESS_CASES_H
#include "transfer_address_oracle.h"
#include <string.h>
#include <stdlib.h>
#define XE_SAMPLE_LANES 10
enum {XE_SAMPLE_CASES=62208,XE_SAMPLE_OUTPUT_CASES=62214};
static void xe_address_key(unsigned index,xe_transfer_address_key *k) {
 static const unsigned scales[6][4]={{1,1,1,1},{2,2,2,2},{3,5,3,5},{1,1,3,5},{3,5,1,1},{1,1,1,3}};
 static const int32_t deltas[]={-2047,-65,-1,0,1,63,2047};
 unsigned n=index/64,scale=n%6,dc,sc,dw,sw,layout;n/=6;
 memset(k,0,sizeof(*k));k->dest_native2=k->source_native2=n%2;n/=2;
 layout=n%9;n/=9;
 /* Three valid color/width states: depth32, color32, color64. */
 dc=layout/3;sc=layout%3;dw=dc==2;sw=sc==2;
 k->dest_color=dc!=0;k->source_color=sc!=0;k->dest_wide=dw;k->source_wide=sw;
 k->source_msaa=n%3;k->dest_msaa=n/3;
 k->dest_scale_x=scales[scale][0];k->dest_scale_y=scales[scale][1];
 k->source_scale_x=scales[scale][2];k->source_scale_y=scales[scale][3];
 k->dest_pitch=index%31+1;k->source_pitch=index%2?index%63+1:1023;
 k->dest_to_source_base=deltas[index%7];
}
static void xe_sample_input(uint32_t *r,unsigned index,unsigned round) {
 static const uint32_t edges[]={0,1,19,20,39,40,79,80,1279,1280,2047,4095,0x80000000u,0xffffffffu};
 xe_transfer_address_key k;xe_transfer_address_config p;unsigned t=(index+round*17)%XE_SAMPLE_CASES,i;
 xe_address_key(t,&k);
 r[0]=t%64<14?edges[t%64]:t*1664525u+round*7919u;
 r[1]=t%64<14?edges[13-t%64]:t*1013904223u+round*13u;
 r[2]=k.dest_msaa==2?(t&3):k.dest_msaa==1?(k.dest_native2?(t&1):(t&1)*3):0;
 if(!xe_transfer_address_prepare(&p,&k))abort();
 for(i=0;i<XE_ADDR_WORDS;++i)r[3+i]=p.word[i];
 /* Oracle-only key words, never consumed by GPU. */
 r[26]=k.dest_color;r[27]=k.source_color;
 for(i=28;i<32;++i)r[i]=0x913b7000u+i;
}
static void xe_sample_expected(const uint32_t *r,uint32_t out[XE_SAMPLE_LANES]) {
 xe_transfer_address_key k;const uint32_t *p=r+3;
 memset(&k,0,sizeof(k));
 k.dest_msaa=p[XE_ADDR_D_MSAA];k.source_msaa=p[XE_ADDR_S_MSAA];
 k.dest_native2=p[XE_ADDR_D_NATIVE2];k.source_native2=p[XE_ADDR_S_NATIVE2];
 k.dest_scale_x=p[XE_ADDR_D_SX];k.dest_scale_y=p[XE_ADDR_D_SY];
 k.source_scale_x=p[XE_ADDR_S_SX];k.source_scale_y=p[XE_ADDR_S_SY];
 k.dest_wide=p[XE_ADDR_D_WIDE];k.source_wide=p[XE_ADDR_S_WIDE];
 k.dest_pitch=p[XE_ADDR_D_PITCH];k.source_pitch=p[XE_ADDR_S_PITCH];
 k.dest_to_source_base=p[XE_ADDR_DELTA]<=2047?(int32_t)p[XE_ADDR_DELTA]:-(int32_t)(0u-p[XE_ADDR_DELTA]);
 k.dest_color=r[26];k.source_color=r[27];legacy(&k,r[0],r[1],r[2],out);
}
#endif
