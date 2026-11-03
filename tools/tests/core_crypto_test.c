#include "libretro/core_crypto.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <lrc_hash.h>
static unsigned nibble(char c) {
  return c <= '9' ? (unsigned)(c - '0') : (unsigned)(c - 'a' + 10);
}
static void vector(const uint8_t *key, size_t key_size,
    const char *text, const char *hex) {
  uint8_t expected[20], out[25];
  size_t i, split, middle, size = strlen(text), cap, written;
  for (i = 0; i < 20; ++i)
    expected[i] = (uint8_t)((nibble(hex[i * 2]) << 4) | nibble(hex[i * 2 + 1]));
  for (split = 0; split <= size; ++split) {
    middle = (size - split) / 2;
    for (cap = 0; cap <= sizeof(out); ++cap) {
      memset(out, 0xa7, sizeof(out));
      assert(xe_hmac_sha1_3(key, key_size, (const uint8_t *)text, split,
          (const uint8_t *)text + split, middle,
          (const uint8_t *)text + split + middle, size - split - middle, out, cap));
      written = cap < 20 ? cap : 20;
      assert(!memcmp(out, expected, written));
      for (i = written; i < sizeof(out); ++i) assert(out[i] == 0xa7);
    }
  }
}
static void md5_vector(const char *text, const char *hex) {
  uint8_t expected[16], out[25];
  size_t i, split, cap, size = strlen(text), written;
  for (i = 0; i < 16; ++i)
    expected[i] = (uint8_t)((nibble(hex[i * 2]) << 4) | nibble(hex[i * 2 + 1]));
  for (split = 0; split <= size; ++split) {
    for (cap = 0; cap <= sizeof(out); ++cap) {
      memset(out, 0xa7, sizeof(out));
      assert(xe_md5_3((const uint8_t *)text, split, NULL, 0,
          (const uint8_t *)text + split, size - split, out, cap));
      written = cap < 16 ? cap : 16;
      assert(!memcmp(out, expected, written));
      for (i = written; i < sizeof(out); ++i) assert(out[i] == 0xa7);
    }
  }
}
int main(void) {
  uint8_t key[80], alias[32], expected[20];
  uint8_t zeros[16], cipher[16];
  static uint8_t md5_input[1025];
  MD5_CTX md5;
  static const uint8_t rc4_expected[16] = {
    0x9a,0xc7,0xcc,0x9a,0x60,0x9d,0x1e,0xf7,
    0xb2,0x93,0x28,0x99,0xcd,0xe4,0x1b,0x97
  };
  unsigned i;
  md5_vector("", "d41d8cd98f00b204e9800998ecf8427e");
  md5_vector("abc", "900150983cd24fb0d6963f7d28e17f72");
  md5_vector("message digest", "f96b697d7cb7938d525a2f31aaf161d0");
  for (i = 0; i < sizeof(md5_input); ++i) md5_input[i] = (uint8_t)(i * 37);
  MD5_Init(&md5);
  for (i = 1; i < sizeof(md5_input); ++i) MD5_Update(&md5, md5_input + i, 1);
  MD5_Final(expected, &md5);
  assert(xe_md5_3(md5_input + 1, 1024, NULL, 0, NULL, 0, alias, 16));
  assert(!memcmp(alias, expected, 16));
  assert(xe_md5_3(md5_input + 1, 1024, NULL, 0, NULL, 0, md5_input + 1, 16));
  assert(!memcmp(md5_input + 1, expected, 16));
  memset(key, 0x0b, 20);
  vector(key, 20, "Hi There", "b617318655057264e28bc0b6fb378c8ef146be00");
  vector((const uint8_t *)"Jefe", 4, "what do ya want for nothing?",
      "effcdf6ae5eb2fa2d27416d5f184df9c259a7c79");
  memset(key, 0xaa, sizeof(key));
  vector(key, sizeof(key), "Test Using Larger Than Block-Size Key - Hash Key First",
      "aa4ae5e15272d00e95705637ce8a3b55ed402112");
  vector(NULL, 0, "", "fbdb1d1b18aa6c08324b7d64b71fb76370690e1d");
  memset(key, 0x0b, 20); memcpy(alias, "Hi There", 8);
  assert(xe_hmac_sha1_3(key, 20, alias, 8, NULL, 0, NULL, 0, expected, 20));
  assert(xe_hmac_sha1_3(key, 20, alias, 8, NULL, 0, NULL, 0, alias, 20));
  assert(!memcmp(alias, expected, 20));
  assert(xe_hmac_sha1_3(key, 20, (const uint8_t *)"Hi There", 8,
      NULL, 0, NULL, 0, key, 20));
  assert(!memcmp(key, expected, 20));
  memset(alias, 0xa7, sizeof(alias));
  assert(!xe_hmac_sha1_3(NULL, 1, NULL, 0, NULL, 0, NULL, 0, alias, 20));
  assert(!xe_hmac_sha1_3(key, 20, NULL, 1, NULL, 0, NULL, 0, alias, 20));
  assert(alias[0] == 0xa7 && alias[19] == 0xa7);
  assert(xe_hmac_sha1_3(NULL, 1, NULL, 1, NULL, 1, NULL, 1, NULL, 0));
  for (i = 0; i < 16; ++i) key[i] = (uint8_t)(i + 1);
  memset(zeros, 0, sizeof(zeros));
  assert(xe_rc4_16(key, 16, zeros, 16, cipher, 16));
  assert(!memcmp(cipher, rc4_expected, 16));
  assert(xe_rc4_16(key, 16, cipher, 16, cipher, 16));
  assert(!memcmp(cipher, zeros, 16));
  memset(alias, 0xa7, sizeof(alias));
  assert(!xe_rc4_16(key, 15, zeros, 16, alias, 16));
  assert(!xe_rc4_16(key, 16, zeros, 16, alias, 15));
  assert(!xe_rc4_16(key, 16, alias, 16, alias + 1, 16));
  assert(!xe_rc4_16(key, 16, alias + 1, 16, alias, 16));
  assert(!xe_rc4_16(NULL, 16, zeros, 16, alias, 16));
  for (i = 0; i < sizeof(alias); ++i) assert(alias[i] == 0xa7);
  assert(xe_rc4_16(NULL, 0, NULL, 0, NULL, 0));
  puts("common MD5/HMAC-SHA1 spans, RC4, truncation, aliasing and vectors: PASS");
  return 0;
}
