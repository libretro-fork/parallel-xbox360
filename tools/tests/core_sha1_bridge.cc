/* Reference interoperability while the guest-state SHA-1 port is pending. */
#include "third_party/crypto/TinySHA1.hpp"
extern "C" void xe_test_legacy_sha1(const unsigned char* data, size_t size,
                                   unsigned char* digest) {
  sha1::SHA1 state;
  state.processBytes(data, size);
  state.finalize(digest);
}
