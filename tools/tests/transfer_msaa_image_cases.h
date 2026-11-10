/* Independent per-physical-sample image oracle, BSD; C89. */
#ifndef XE_TRANSFER_MSAA_IMAGE_CASES_H
#define XE_TRANSFER_MSAA_IMAGE_CASES_H
#include "transfer_uint_image_cases.h"
static INLINE uint32_t image_ms_pattern(unsigned format,unsigned x,unsigned y,
 unsigned sample,unsigned lane) {
 uint32_t v;
 switch(lane) {
  case 0:v=0x7fc00000u^x^(y<<8)^(sample<<20);break;
  case 1:v=0x80008000u^(x<<4)^y^(sample<<12);break;
  case 2:v=0xffff0000u^(x<<8)^(y<<1)^(sample<<14);break;
  default:v=0x0000ffffu^x^(y<<8)^(sample<<4);break;
 }
 return image_halves(format)?v&65535:v;
}
static INLINE void image_ms_words(unsigned format,unsigned x,unsigned y,
 unsigned sample,uint32_t out[2]) {
 uint32_t a=image_ms_pattern(format,x,y,sample,0),b=image_ms_pattern(format,x,y,sample,1);
 if(image_halves(format)) {
  out[0]=a|(b<<16);out[1]=image_components(format)==4?
   image_ms_pattern(format,x,y,sample,2)|(image_ms_pattern(format,x,y,sample,3)<<16):0;
 } else {out[0]=a;out[1]=format==15?b:0;}
}
static INLINE int image_ms_active(const xe_transfer_address_key *key,unsigned sample) {
 return key->dest_msaa==0?sample==0:key->dest_msaa==1?
  (key->dest_native2?sample<2:(sample==0 || sample==3)):sample<4;
}
static INLINE void image_ms_expected(const xe_transfer_address_key *key,unsigned sf,
 unsigned df,unsigned x,unsigned y,unsigned sample,uint32_t out[4]) {
 uint32_t route[10],a[2],b[2],lo,hi;
 if(!image_ms_active(key,sample)){out[0]=out[1]=out[2]=out[3]=7;return;}
 legacy(key,x,y,sample,route);image_ms_words(sf,route[0],route[1],route[2],a);
 lo=a[route[4]];hi=a[1];
 if(route[5]){image_ms_words(sf,route[3],route[1],route[2],b);hi=b[0];}
 if(image_halves(df)) {
  out[0]=lo&65535;out[1]=lo>>16;
  out[2]=image_components(df)==4?hi&65535:0;
  out[3]=image_components(df)==4?hi>>16:0;
 } else {out[0]=lo;out[1]=df==15?hi:0;out[2]=out[3]=0;}
}
#endif
