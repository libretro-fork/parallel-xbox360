/* Known-answer and incremental boundary tests for the owned common SHA-256. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <lrc_hash.h>
#ifdef XE_SHA_TEST_SCALAR
/* Compile the implementation with cpu_features_get renamed to this probe. */
uint64_t xe_test_cpu_features_get(void) { return 0; }
#endif

static void hex(const uint8_t *in, char *out) {
  static const char digits[] = "0123456789abcdef";
  unsigned i;
  for (i = 0; i < 32; ++i) {
    out[i * 2] = digits[in[i] >> 4];
    out[i * 2 + 1] = digits[in[i] & 15];
  }
  out[64] = 0;
}
static void vector(const uint8_t *data, size_t size, const char *expected) {
  struct sha256_state state;
  uint8_t digest[32];
  char text[65];
  size_t split;
  for (split = 0; split <= size; ++split) {
    sha256_stream_init(&state, 0);
    sha256_stream_update(&state, data, split);
    sha256_stream_update(&state, NULL, 0);
    sha256_stream_update(&state, data + split, size - split);
    sha256_stream_final(&state, digest);
    hex(digest, text); assert(!strcmp(text, expected));
  }
}
int main(void) {
  static const uint8_t message[] =
      "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  uint8_t data[1025], one[32], chunks[32];
  struct sha256_state a, b;
  size_t length, chunk, at, n;
  uint8_t sha1[20], many[40];
  char text[65];
  static const uint8_t sha1_abc[20] = {
    0xa9,0x99,0x3e,0x36,0x47,0x06,0x81,0x6a,0xba,0x3e,
    0x25,0x71,0x78,0x50,0xc2,0x6c,0x9c,0xd0,0xd8,0x9d
  };
  SHA1Digest((const uint8_t *)"abc", 3, sha1);
  assert(!memcmp(sha1, sha1_abc, 20));
  SHA1DigestMany((const uint8_t *)"abcd", 1, 3, 2, many);
  assert(!memcmp(many, sha1_abc, 20));
  SHA1Digest((const uint8_t *)"bcd", 3, sha1);
  assert(!memcmp(many + 20, sha1, 20));
  vector((const uint8_t *)"", 0,
      "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  vector((const uint8_t *)"abc", 3,
      "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  vector(message, sizeof(message) - 1,
      "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
  memset(data, 'a', 1000);
  sha256_stream_init(&a, 0);
  for (at = 0; at < 1000; ++at) sha256_stream_update(&a, data, 1000);
  sha256_stream_final(&a, one); hex(one, text);
  assert(!strcmp(text, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
  sha256_stream_init(&a, 1);
  sha256_stream_update(&a, (const uint8_t *)"abc", 3);
  memset(one, 0, sizeof(one)); sha256_stream_final(&a, one); hex(one, text);
  assert(!strncmp(text, "23097d223405d8228642a477bda255b32aadbce4bda0b3f7e36c9da7", 56));
  memset(data, 0, 64); memcpy(data, "abc\200", 4); data[63] = 24;
  sha256_stream_init(&a, 0); sha256_stream_block(&a, data);
  assert(!a.len && !a.inlen && a.h[0] == 0xba7816bfU && a.h[7] == 0xf20015adU);
  for (at = 0; at < sizeof(data); ++at) data[at] = (uint8_t)(at * 37 + 9);
  /* Include unaligned input and every padding boundary through 1 KiB. */
  for (length = 0; length < sizeof(data); ++length) {
    sha256_stream_init(&a, 0);
    sha256_stream_update(&a, data + 1, length);
    sha256_stream_final(&a, one);
    for (chunk = 1; chunk <= 129; chunk += 16) {
      sha256_stream_init(&b, 0);
      for (at = 0; at < length; at += n) {
        n = length - at; if (n > chunk) n = chunk;
        sha256_stream_update(&b, data + 1 + at, n);
      }
      sha256_stream_final(&b, chunks);
      assert(!memcmp(one, chunks, 32));
    }
  }
  puts("common SHA-256 vectors and incremental boundaries: PASS");
  return 0;
}
