/* Independent raw image patterns and legacy physical routes. BSD; C89. */
#ifndef XE_TRANSFER_UINT_IMAGE_CASES_H
#define XE_TRANSFER_UINT_IMAGE_CASES_H
#include "transfer_address_oracle.h"
#include "libretro/core_transfer_uint.h"
static INLINE unsigned image_components(unsigned format) {
 return format==5 || format==7?4:format==14?1:2;
}
static INLINE unsigned image_halves(unsigned format){return format<14;}
static INLINE uint32_t image_pattern(unsigned format,unsigned x,unsigned y,unsigned lane) {
 static const uint32_t edges[]={0,0xffffffffu,0x80000000u,0x7fc12345u,0x7f800000u,0xff800000u,0x80018000u,0x7e01fc00u};
 uint32_t n=x*1664525u+y*1013904223u+lane*7919u,v;
 v=(x+y+lane)%3?edges[(x+lane+y)%8]:n^(n>>13);
 return image_halves(format)?v&65535:v;
}
static INLINE void image_words(unsigned format,unsigned x,unsigned y,uint32_t words[2]) {
 if(image_halves(format)) {
  words[0]=image_pattern(format,x,y,0)|(image_pattern(format,x,y,1)<<16);
  words[1]=image_components(format)==4?
   image_pattern(format,x,y,2)|(image_pattern(format,x,y,3)<<16):0;
 } else {
  words[0]=image_pattern(format,x,y,0);
  words[1]=format==15?image_pattern(format,x,y,1):0;
 }
}
static INLINE void image_expected(const xe_transfer_address_key *key,unsigned sf,unsigned df,
 unsigned x,unsigned y,uint32_t out[4]) {
 uint32_t route[10],a[2],b[2],lo,hi;
 legacy(key,x,y,0,route);image_words(sf,route[0],route[1],a);
 lo=a[route[4]];hi=a[1];
 if(route[5]){image_words(sf,route[3],route[1],b);hi=b[0];}
 if(image_halves(df)) {
  out[0]=lo&65535;out[1]=lo>>16;
  out[2]=image_components(df)==4?hi&65535:0;
  out[3]=image_components(df)==4?hi>>16:0;
 } else {out[0]=lo;out[1]=df==15?hi:0;out[2]=out[3]=0;}
}
#endif
