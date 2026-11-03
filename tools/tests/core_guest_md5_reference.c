/* Test-only bridge to the actual FFmpeg implementation, with stack state. */
#include "core_guest_hash_reference.h"
#include "config.h"
#include "third_party/FFmpeg/libavutil/md5.c"
void xe_reference_md5(uint8_t *guest, const uint8_t *input, size_t size,
    uint8_t *out, size_t cap, int operation) {
  AVMD5 p;
  uint8_t digest[16];
  unsigned i;
  if (!operation) {
    memset(guest, 0, 88); av_md5_init(&p);
    for (i=0;i<4;++i) ref_put32(guest+8+i*4,p.ABCD[i]);
    return;
  }
  p.len=ref_be64(guest); memcpy(p.block,guest+24,64);
  for(i=0;i<4;++i) p.ABCD[i]=ref_be32(guest+8+i*4);
  if(operation==1) {
    if(size) av_md5_update(&p,input,size);
    ref_put64(guest,p.len); memcpy(guest+24,p.block,64);
    for(i=0;i<4;++i) ref_put32(guest+8+i*4,p.ABCD[i]);
  } else {
    av_md5_final(&p,digest);
    if(cap) memcpy(out,digest,cap<16?cap:16);
    memcpy(guest+24,digest,16);
  }
}
