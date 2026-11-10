/* Independent legacy address mapping. Xenia authors; BSD; C89. */
#ifndef XE_TEST_TRANSFER_ADDRESS_ORACLE_H
#define XE_TEST_TRANSFER_ADDRESS_ORACLE_H
#include "libretro/core_transfer_address.h"
static void legacy(const xe_transfer_address_key *k,uint32_t px,uint32_t py,uint32_t sample,uint32_t out[10]) {
 uint32_t dw=(80>>(k->dest_wide+(k->dest_msaa==2)))*k->dest_scale_x;
 uint32_t dh=(16>>(k->dest_msaa!=0))*k->dest_scale_y;
 uint32_t sw=(80>>(k->source_wide+(k->source_msaa==2)))*k->source_scale_x;
 uint32_t sh=(16>>(k->source_msaa!=0))*k->source_scale_y;
 uint32_t tile=py/dh*k->dest_pitch+px/dw,x=px%dw,y=py%dh,u,v,gx,gy,sub_x,sub_y,s,half=0;
 out[7]=tile;out[8]=x;out[9]=y;
 if(k->dest_scale_x!=k->source_scale_x || k->dest_scale_y!=k->source_scale_y) {
  if(k->dest_scale_x==1 && k->dest_scale_y==1) {
   x=x*k->source_scale_x+k->source_scale_x/2;y=y*k->source_scale_y+k->source_scale_y/2;
  } else {x/=k->dest_scale_x;y/=k->dest_scale_y;}
 }
 if(k->dest_msaa!=k->source_msaa || k->dest_wide!=k->source_wide) {
  gx=x/k->source_scale_x;gy=y/k->source_scale_y;
  sub_x=x%k->source_scale_x;sub_y=y%k->source_scale_y;
  if(k->dest_msaa==2) {
   u=(gx&1)|((sample&1)<<1)|((gx>>1)<<2);v=(gy&1)|(sample&2)|((gy>>1)<<2);
  } else if(k->dest_msaa==1) {
   s=k->dest_native2?(sample^1):(sample>>1);
   u=(gx&0xfffffffdu)|((s&1)<<1);v=(gy&1)|(gx&2)|((gy>>1)<<2);
  } else {u=gx;v=gy;}
  if(k->dest_wide && !k->source_wide)u<<=1;
  else if(!k->dest_wide && k->source_wide){half=u&1;u>>=1;}
  if(k->source_msaa==2) {
   gx=(u&1)|((u>>2)<<1);gy=(v&1)|((v>>2)<<1);sample=((u>>1)&1)|(v&2);
  } else if(k->source_msaa==1) {
   gx=(u&0xfffffffdu)|(v&2);gy=(v&1)|((v>>2)<<1);s=(u>>1)&1;
   sample=k->source_native2?(s^1):s*3;
  } else {gx=u;gy=v;sample=0;}
  x=gx*k->source_scale_x+sub_x;y=gy*k->source_scale_y+sub_y;
 }
 if(k->source_color!=k->dest_color)x=x<sw/2?x+sw/2:x-sw/2;
 tile=(tile+(uint32_t)k->dest_to_source_base)&2047;
 out[0]=(tile%k->source_pitch)*sw+x;out[1]=(tile/k->source_pitch)*sh+y;
 out[2]=sample;out[3]=out[0]+k->source_scale_x;out[4]=half;
 out[5]=k->dest_wide && !k->source_wide;out[6]=tile;
}
#endif
