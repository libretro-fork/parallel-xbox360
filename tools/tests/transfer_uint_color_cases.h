/* Independent raw integer ownership-view payload oracles. BSD; C89. */
#ifndef XE_TEST_TRANSFER_UINT_COLOR_CASES_H
#define XE_TEST_TRANSFER_UINT_COLOR_CASES_H
#include "libretro/core_transfer_uint_color.h"
#include <stdlib.h>
enum {XE_SAMPLE_CASES=36867,XE_SAMPLE_OUTPUT_CASES=36873};
static uint32_t xe_uint_color_hash(uint32_t n) {
 n^=n>>16;n*=0x7feb352du;n^=n>>15;n*=0x846ca68bu;return n^(n>>16);
}
static void xe_sample_input(uint32_t *r,unsigned index,unsigned round) {
 static const unsigned formats[]={4,5,6,7,14,15};
 static const uint32_t edges[]={0,1,0x8000u,65535,65536,0xffff8000u,
  0x7c007e01u,0xfc007e00u,0x7f800001u,0xff800001u,0x80000000u,0xffffffffu};
 xe_transfer_uint_color_config config;unsigned i,n=(index+round*31)%36864;
 r[10]=formats[(n/1024)%6];r[11]=formats[n/(1024*6)];
 for(i=0;i<6;++i)r[i]=n%1024<12?edges[(n+i)%12]:xe_uint_color_hash(index*7+i+round*127);
 if(!xe_transfer_uint_color_prepare(&config,r[10]))abort();
 r[6]=config.halves;r[7]=config.wide;
 if(!xe_transfer_uint_color_prepare(&config,r[11]))abort();
 r[8]=config.halves;r[9]=config.wide;
 for(i=12;i<32;++i)r[i]=0x983da000u+i;
}
static void xe_sample_expected(const uint32_t *r,uint32_t out[7]) {
 unsigned format=r[10],i;
 if(format<14) {
  out[0]=(r[0]&65535)|((r[1]&65535)<<16);
  out[1]=format==5 || format==7?(r[2]&65535)|((r[3]&65535)<<16):0;
 } else {out[0]=r[0];out[1]=format==15?r[1]:0;}
 out[6]=format==5 || format==7 || format==15?2:1;
 for(i=2;i<6;++i)out[i]=0;
 format=r[11];
 if(format<14) {
  out[2]=r[4]&65535;out[3]=r[4]>>16;
  if(format==5 || format==7){out[4]=r[5]&65535;out[5]=r[5]>>16;}
 } else {out[2]=r[4];if(format==15)out[3]=r[5];}
}
#endif
