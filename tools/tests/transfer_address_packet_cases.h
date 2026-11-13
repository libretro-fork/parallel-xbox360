/* Compact GPU inputs plus oracle-only logical keys. BSD; MSVC C89. */
#ifndef XE_TEST_TRANSFER_ADDRESS_PACKET_CASES_H
#define XE_TEST_TRANSFER_ADDRESS_PACKET_CASES_H
#include "libretro/core_transfer_address_packet.h"
#include "transfer_address_oracle.h"
#include <string.h>
#include <stdlib.h>
#ifndef XE_TRANSFER_PACKET_NATIVE2
#define XE_TRANSFER_PACKET_NATIVE2 0
#endif
#define XE_SAMPLE_LANES 10
enum {XE_SAMPLE_CASES=20736,XE_SAMPLE_OUTPUT_CASES=20742};
static void xe_sample_input(uint32_t *r,unsigned index,unsigned round) {
 static const uint32_t edges[]={0,1,19,20,39,40,79,80,1279,1280,2047,4095,0x80000000u,0xffffffffu};
 static const int32_t deltas[]={-2047,-65,-1,0,1,63,2047};
 xe_transfer_address_packet_config config={3,5,XE_TRANSFER_PACKET_NATIVE2};
 xe_transfer_address_key k={0};xe_transfer_address_packet packet;unsigned t=(index+round*17)%XE_SAMPLE_CASES,n=t/64,layout,scale,i;uint32_t sample;
 k.dest_msaa=n%3;n/=3;k.source_msaa=n%3;n/=3;layout=n%9;scale=n/9;
 k.dest_native2=k.source_native2=config.native2;k.dest_color=layout/3!=0;k.source_color=layout%3!=0;
 k.dest_wide=layout/3==2;k.source_wide=layout%3==2;
 k.dest_scale_x=scale&1?1:3;k.dest_scale_y=scale&1?1:5;k.source_scale_x=scale&2?1:3;k.source_scale_y=scale&2?1:5;
 k.dest_pitch=t%31+1;k.source_pitch=t%2?t%63+1:1023;k.dest_to_source_base=deltas[t%7];
 r[0]=t%64<14?edges[t%64]:t*1664525u+round*7919u;
 r[1]=t%64<14?edges[13-t%64]:t*1013904223u+round*13u;
 sample=k.dest_msaa==2?(t&3):k.dest_msaa==1?(config.native2?(t&1):(t&1)*3):0;
 if(!xe_transfer_address_packet_prepare(&packet,&config,&k,sample))abort();
 r[2]=packet.address;r[3]=packet.geometry;r[4]=packet.dest_reciprocal_x;r[5]=packet.dest_reciprocal_y;
 r[6]=packet.source_pitch_reciprocal;r[7]=packet.sample;
 /* Never consumed by the GPU: independent oracle inputs, not packet decoding. */
 r[8]=k.dest_msaa;r[9]=k.source_msaa;r[10]=k.dest_native2;r[11]=k.dest_scale_x;r[12]=k.dest_scale_y;
 r[13]=k.source_scale_x;r[14]=k.source_scale_y;r[15]=k.dest_wide;r[16]=k.source_wide;
 r[17]=k.dest_color;r[18]=k.source_color;r[19]=k.dest_pitch;r[20]=k.source_pitch;r[21]=(uint32_t)k.dest_to_source_base;r[22]=sample;
 for(i=23;i<32;++i)r[i]=0x913b7000u+i;
}
static void xe_sample_expected(const uint32_t *r,uint32_t out[XE_SAMPLE_LANES]) {
 xe_transfer_address_key k={0};k.dest_msaa=r[8];k.source_msaa=r[9];k.dest_native2=k.source_native2=r[10];
 k.dest_scale_x=r[11];k.dest_scale_y=r[12];k.source_scale_x=r[13];k.source_scale_y=r[14];
 k.dest_wide=r[15];k.source_wide=r[16];k.dest_color=r[17];k.source_color=r[18];k.dest_pitch=r[19];k.source_pitch=r[20];
 k.dest_to_source_base=r[21]<=2047?(int32_t)r[21]:-(int32_t)(0u-r[21]);legacy(&k,r[0],r[1],r[22],out);
}
#endif
