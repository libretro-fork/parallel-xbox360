/* Borrowed packet/chain preflight, overlap semantics and no-write failures. C89. */
#include <stdio.h>
#include <string.h>
#include "core_xex_delta.h"
#include <lrc_hash.h>
static uint8_t patch[256], image[128], expected[128], blocks[128], digest[20];
static unsigned decoded;
static int decoder_error;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
static void put32(uint8_t *p, uint32_t n) {
  p[0] = (uint8_t)(n >> 24); p[1] = (uint8_t)(n >> 16);
  p[2] = (uint8_t)(n >> 8); p[3] = (uint8_t)n;
}
static void packet(uint8_t *p, uint32_t old, uint32_t target, unsigned unpacked, unsigned compressed) {
  put32(p, old); put32(p + 4, target);
  p[8] = (uint8_t)(unpacked >> 8); p[9] = (uint8_t)unpacked;
  p[10] = (uint8_t)(compressed >> 8); p[11] = (uint8_t)compressed;
}
static void reset(void) {
  unsigned i;
  memset(patch, 0, sizeof(patch)); decoded = 0; decoder_error = 0;
  for (i = 0; i < sizeof(image); ++i) image[i] = (uint8_t)(i * 37 + 5);
  memcpy(expected, image, sizeof(image));
}
static int decode(void *opaque, const uint8_t *data, size_t size,
    uint8_t *out, size_t length, uint32_t window, const uint8_t *reference, size_t refsize) {
  size_t i;
  (void)opaque; (void)window;
  ++decoded;
  if (decoder_error) return decoder_error;
  if (refsize != length || size != length) return 7;
  for (i = 0; i < length; ++i) out[i] = reference[i] ^ data[i];
  return 0;
}
static void hash(void *opaque, const uint8_t *p, size_t n, uint8_t *out) {
  (void)opaque; SHA1Digest(p, n, out);
}
#ifdef XE_DELTA_BRIDGE
extern int xe_test_delta_decoder(const uint8_t *, size_t, uint8_t *, size_t, size_t);
extern int xe_test_delta_module(int, int);
extern int xe_test_lzx_adapter(void);
extern int xe_test_delta_frame(const uint8_t *, size_t, uint8_t *, size_t);
#endif
int main(void) {
  xe_delta_chain chain;
  size_t i, n;
  unsigned fault, mode;
  uint32_t rng = 12345;
  reset(); packet(patch, 0, 3, 31, 1); packet(patch + 12, 0, 60, 7, 0);
  memmove(expected + 3, expected, 31); memset(expected + 60, 0, 7);
  CHECK(!xe_delta_apply(patch, 36, 0, image, 128, 128, decode, NULL));
  CHECK(!decoded && !memcmp(image, expected, 128));
  reset(); packet(patch, 3, 0, 31, 1); memmove(expected, expected + 3, 31);
  CHECK(!xe_delta_apply(patch, 12, 0, image, 128, 128, decode, NULL));
  CHECK(!memcmp(image, expected, 128));
#ifdef XE_DELTA_BRIDGE
  reset(); packet(patch, 0, 50, 8, 24);
  memset(patch + 12, 0, 24); patch[13] = 0x30; patch[14] = 0x80;
  patch[16] = patch[20] = patch[24] = 1;
  memcpy(patch + 28, "DELTAXYZ", 8);
  CHECK(!xe_test_delta_decoder(patch, 36, image, 128, 128));
  CHECK(!memcmp(image + 50, "DELTAXYZ", 8));
  CHECK(xe_test_delta_frame(patch + 12, 24, image + 50, 8));
  CHECK(!memcmp(image + 50, "DELTAXYZ", 8));
  CHECK(xe_test_lzx_adapter());
#endif
  reset(); packet(patch, 0, 50, 9, 9);
  for (i = 0; i < 9; ++i) patch[12 + i] = (uint8_t)(i + 7);
  for (i = 0; i < 9; ++i) expected[50 + i] = expected[i] ^ patch[12 + i];
  CHECK(!xe_delta_apply(patch, 21, 32768, image, 128, 128, decode, NULL));
  CHECK(decoded == 1 && !memcmp(image, expected, 128));
  for (fault = 0; fault < 8; ++fault) {
    reset(); packet(patch, 0, 60, 7, 0); packet(patch + 12, 0, 30, 8, 2);
    switch (fault) {
      case 0: put32(patch + 16, 127); break;
      case 1: put32(patch + 12, 127); break;
      case 2: patch[22] = 255; patch[23] = 255; break;
      case 3: break;
      case 4: put32(patch + 16, 0xffffffffU); break;
      case 5: put32(patch + 12, 0xffffffffU); break;
      case 6: break;
      default: break;
    }
    CHECK(xe_delta_apply(patch, fault == 3 ? 23 : 26,
        fault == 6 ? 0 : fault == 7 ? 4 : 32768, image, 128, 128, decode, NULL));
    CHECK(!decoded && !memcmp(image, expected, 128));
  }
  reset(); packet(patch, 0, 50, 8, 8); decoder_error = 19;
  CHECK(xe_delta_apply(patch, 20, 32768, image, 128, 128, decode, NULL) == 19);
  CHECK(!memcmp(image, expected, 128));
  for (i = 1; i < 12; ++i) CHECK(!xe_delta_validate(patch, i, 32768, 128, 128));
  CHECK(!xe_delta_validate(NULL, 12, 32768, 128, 128));
  CHECK(xe_delta_validate(NULL, 0, 0, 128, 128));
  CHECK(xe_delta_apply(image, 12, 32768, image, 128, 128, decode, NULL));
  /* Two hashed blocks with borrowed packet views; second-block failure stops
   * the complete preflight before the first block has been applied. */
  reset(); memset(blocks, 0, sizeof(blocks)); put32(blocks, 36);
  packet(blocks + 24, 0, 3, 31, 1); packet(blocks + 60, 0, 60, 7, 0);
  SHA1Digest(blocks + 36, 36, blocks + 4); SHA1Digest(blocks, 36, digest);
  CHECK(xe_delta_chain_validate(&chain, blocks, 72, 36, digest, 32768, 128, 128, hash, NULL));
  CHECK(chain.input == blocks && chain.length == 72);
  CHECK(!xe_delta_chain_apply(&chain, image, decode, NULL));
  memmove(expected + 3, expected, 31); memset(expected + 60, 0, 7);
  CHECK(!memcmp(image, expected, 128));
  blocks[60 + 7] = 127;
  SHA1Digest(blocks + 36, 36, blocks + 4); SHA1Digest(blocks, 36, digest);
  CHECK(!xe_delta_chain_validate(&chain, blocks, 72, 36, digest, 32768, 128, 128, hash, NULL));
  blocks[60 + 7] = 60; blocks[70] ^= 1;
  CHECK(!xe_delta_chain_validate(&chain, blocks, 72, 36, digest, 32768, 128, 128, hash, NULL));
  CHECK(!xe_delta_chain_validate(&chain, blocks, 35, 36, digest, 32768, 128, 128, hash, NULL));
  CHECK(!xe_delta_chain_validate(&chain, blocks, 72, 23, digest, 32768, 128, 128, hash, NULL));
  /* Broad random packet wire, validated against fixed canary-backed storage. */
  for (n = 0; n < 10000; ++n) {
    reset();
    for (i = 0; i < 64; ++i) {
      rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; patch[i] = (uint8_t)rng;
    }
    mode = xe_delta_validate(patch, 64, 32768, 128, 128) ? 1u : 0u;
    if (!mode) CHECK(xe_delta_apply(patch, 64, 32768, image, 128, 128, decode, NULL));
    if (!mode) CHECK(!decoded && !memcmp(image, expected, 128));
  }
#ifdef XE_DELTA_BRIDGE
  reset(); packet(patch, 0, 3, 31, 1); packet(patch + 12, 0, 60, 7, 0);
  memmove(expected + 3, expected, 31); memset(expected + 60, 0, 7);
  CHECK(!xe_test_delta_decoder(patch, 36, image, 128, 128));
  CHECK(!memcmp(image, expected, 128));
  for (mode = 0; mode < 2; ++mode) for (fault = 0; fault < 2; ++fault)
    CHECK(xe_test_delta_module((int)mode, (int)fault));
  puts("Actual delta patch modules: plaintext/encrypted overlap patches and corrupt-chain no-write pass");
#endif
  puts("Delta: borrowed packet/hash-chain bounds, overlap moves and complete no-write preflight pass");
  return 0;
}
