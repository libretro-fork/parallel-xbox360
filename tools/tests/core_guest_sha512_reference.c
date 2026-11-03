/* Correctly typed SHA-512 oracle; the former kernel Final used SHA-256 layout. */
#include "core_guest_hash_reference.h"
#include "third_party/FFmpeg/libavutil/sha512.c"
void xe_reference_sha512(uint8_t *guest, const uint8_t *input, size_t size,
    uint8_t *out, size_t cap, int operation) {
  AVSHA512 p;
  uint8_t digest[64];
  unsigned i;
  av_sha512_init(&p,512);
  if(!operation) {
    memset(guest,0,200);
    for(i=0;i<8;++i) ref_put64(guest+8+i*8,p.state[i]);
    return;
  }
  p.count=ref_be64(guest); memcpy(p.buffer,guest+72,128);
  for(i=0;i<8;++i) p.state[i]=ref_be64(guest+8+i*8);
  if(operation==1) {
    if(size) av_sha512_update(&p,input,size);
    ref_put64(guest,p.count); memcpy(guest+72,p.buffer,128);
    for(i=0;i<8;++i) ref_put64(guest+8+i*8,p.state[i]);
  } else {
    av_sha512_final(&p,digest);
    if(cap) memcpy(out,digest,cap<64?cap:64);
    memcpy(guest+72,digest,64);
  }
}
