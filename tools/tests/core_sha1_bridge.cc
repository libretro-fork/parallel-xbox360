/* Reference interoperability while the guest-state SHA-1 port is pending. */
#include "TinySHA1.hpp"
extern "C" void xe_test_legacy_sha1(const unsigned char* data, size_t size,
                                   unsigned char* digest) {
  sha1::SHA1 state;
  state.processBytes(data, size);
  state.finalize(digest);
}
extern "C" void xe_test_legacy_guest_sha1(uint32_t* words,uint8_t* buffer,
    uint32_t* count,const uint8_t* input,size_t size,uint8_t* digest,int final) {
  sha1::SHA1 p;
  p.init(words,buffer,*count);
  if(final) {
    p.finalize(digest);
    std::memcpy(words,p.getDigest(),20);
    std::memcpy(buffer,p.getBlock(),64);
  } else {
    if(size) p.processBytes(input,size);
    std::memcpy(words,p.getDigest(),20);
    if(p.getBlockByteIndex()) std::memcpy(buffer,p.getBlock(),p.getBlockByteIndex());
    *count=static_cast<uint32_t>(p.getByteCount());
  }
}
