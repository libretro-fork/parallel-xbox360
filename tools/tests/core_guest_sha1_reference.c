/* Independent standard SHA-1 oracle with the guest's publication semantics. */
#include "core_guest_hash_reference.h"
#include "third_party/FFmpeg/libavutil/sha.c"
void xe_reference_sha1(uint8_t *guest,const uint8_t *input,size_t size,
    uint8_t *out,size_t cap,int operation) {
  AVSHA p;
  uint8_t digest[20];
  unsigned i,tail;
  av_sha_init(&p,160);
  if(!operation) {
    memset(guest,0,88);
    for(i=0;i<5;++i) ref_put32(guest+4+i*4,p.state[i]);
    return;
  }
  p.count=ref_be32(guest); memcpy(p.buffer,guest+24,64);
  for(i=0;i<5;++i) p.state[i]=ref_be32(guest+4+i*4);
  if(operation==1) {
    if(size) av_sha_update(&p,input,size);
    for(i=0;i<5;++i) ref_put32(guest+4+i*4,p.state[i]);
    tail=(unsigned)(p.count&63);
    if(tail) memcpy(guest+24,p.buffer,tail);
    ref_put32(guest,(uint32_t)p.count);
  } else {
    av_sha_final(&p,digest);
    if(cap) memcpy(out,digest,cap<20?cap:20);
    for(i=0;i<5;++i) ref_put32(guest+4+i*4,p.state[i]);
    memcpy(guest+24,p.buffer,64);
  }
}
void xe_reference_hmac_sha1_init(uint8_t *guest,const uint8_t *key,size_t size) {
  uint8_t pad[64];
  unsigned i;
  memset(pad,0,64);
  if(size>64) size=64;
  if(size) memcpy(pad,key,size);
  memset(guest,0,176);
  for(i=0;i<64;++i) pad[i]^=0x36;
  xe_reference_sha1(guest,NULL,0,NULL,0,0);
  xe_reference_sha1(guest,pad,64,NULL,0,1);
  for(i=0;i<64;++i) pad[i]^=0x6a;
  xe_reference_sha1(guest+88,NULL,0,NULL,0,0);
  xe_reference_sha1(guest+88,pad,64,NULL,0,1);
}
void xe_reference_hmac_sha1_final(uint8_t *guest,uint8_t *out,size_t cap) {
  xe_reference_sha1(guest,NULL,0,NULL,0,2);
  xe_reference_sha1(guest+88,guest+4,20,NULL,0,1);
  xe_reference_sha1(guest+88,NULL,0,out,cap,2);
}
