/* C89 wire/ownership/known-answer/compaction tests. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned allocations, frees;
static int fail_allocation;
static void *test_realloc(void *p, size_t n) {
  ++allocations; if (fail_allocation) return NULL; return realloc(p, n);
}
static void test_free(void *p) { if (p) ++frees; free(p); }
#define XE_BYTES_REALLOC test_realloc
#define XE_BYTES_FREE test_free
#include "core_byte_storage.h"
#include "core_xex_image.h"
#include <lrc_hash.h>
#include "third_party/crypto/rijndael-alg-fst.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
static uint8_t header[1024], chain[128], workspace[128], output[128], first_hash[20];
typedef struct cipher { u32 key[4 * (MAXNR + 1)]; int rounds; uint8_t iv[16]; } cipher;
static void decrypt(void *opaque, const uint8_t *from, uint8_t *to) {
  cipher *c = (cipher *)opaque; rijndaelDecrypt(c->key, c->rounds, from, to);
}
static int decrypt_span(void *opaque, const uint8_t *from, uint8_t *to, size_t n) {
  cipher *c = (cipher *)opaque; return xe_xex_cbc(c, decrypt, c->iv, from, n, to, n);
}
static void hash(void *opaque, const uint8_t *data, size_t n, uint8_t *out) {
  (void)opaque; SHA1Digest(data, n, out);
}
static void put32(uint8_t *p, uint32_t value) {
  p[0] = (uint8_t)(value >> 24); p[1] = (uint8_t)(value >> 16);
  p[2] = (uint8_t)(value >> 8); p[3] = (uint8_t)value;
}
static void hex(uint8_t *out, const char *text) {
  unsigned a, b;
  while (*text) {
    a = (unsigned)(*text <= '9' ? *text - '0' : *text - 'a' + 10); ++text;
    b = (unsigned)(*text <= '9' ? *text - '0' : *text - 'a' + 10); ++text;
    *out++ = (uint8_t)((a << 4) | b);
  }
}
static void make_chain(void) {
  memset(chain, 0, sizeof(chain));
  put32(chain, 64); chain[25] = 11; memcpy(chain + 26, "hello world", 11);
  chain[89] = 13; memcpy(chain + 90, "goodbye world", 13);
  SHA1Digest(chain + 64, 64, chain + 4); SHA1Digest(chain, 64, first_hash);
}
static void make_header(unsigned format) {
  uint32_t fixed = format == 2 ? 0x184 : format == 1 ? 0x168 : 0x154;
  memset(header, 0, sizeof(header));
  put32(header, format == 2 ? 0x58455832U : format == 1 ? 0x58455831U : 0x58455825U);
  put32(header + 8, sizeof(header)); put32(header + 16, 64); put32(header + 20, 1);
  put32(header + 24, 0x000003ffU); put32(header + 28, 800);
  put32(header + 800, 36); header[807] = 2; put32(header + 808, 32768);
  put32(header + 64, fixed + 24);
  put32(header + 64 + fixed - 4, 1); put32(header + 64 + fixed, 0x11);
}
#ifdef XE_XEX_BRIDGE
static uint8_t module_image[68000], expected_image[65536], lzx[65552];
extern int xe_test_xex_module(const uint8_t *, size_t, const uint8_t *, size_t, int);
extern int xe_test_xex_patch_without_descriptor(const uint8_t *, size_t);
static size_t make_module(unsigned compression, int encrypted_mode) {
  static const uint8_t retail_key[16] = {0x20,0xb1,0x85,0xa5,0x9d,0x28,0xfd,0xc3,0x40,0x58,0x3f,0xbb,0x08,0x96,0xbf,0x91};
  uint8_t session[16], iv[16], block[16];
  u32 key_schedule[4 * (MAXNR + 1)];
  size_t length, i, j;
  int rounds;
  make_header(2); memcpy(module_image, header, 1024);
  put32(module_image + 4, 1); put32(module_image + 64 + 4, 65536);
  put32(module_image + 64 + 0x110, 0x82000000U);
  put32(module_image + 800, compression == 2 ? 36 : compression == 1 ? 24 : 8);
  module_image[805] = (uint8_t)encrypted_mode; module_image[807] = (uint8_t)compression;
  for (i = 0; i < 65536; ++i) expected_image[i] = (uint8_t)(i * 37);
  memcpy(expected_image, "MZ\220\000", 4);
  if (!compression) {
    length = 65536; memcpy(module_image + 1024, expected_image, length);
  } else if (compression == 1) {
    length = 96;
    memset(expected_image + 32, 0, 16); memset(expected_image + 112, 0, 65536 - 112);
    put32(module_image + 808, 32); put32(module_image + 812, 16);
    put32(module_image + 816, 64); put32(module_image + 820, 0);
    memcpy(module_image + 1024, expected_image, 32);
    memcpy(module_image + 1056, expected_image + 48, 64);
  } else {
    memset(lzx, 0, 16); lzx[0] = 0x10; lzx[1] = 0x30;
    lzx[4] = lzx[8] = lzx[12] = 1;
    memcpy(lzx + 16, expected_image, 65536);
    memset(module_image + 1024, 0, 65584);
    module_image[1024 + 24] = 0x80;
    memcpy(module_image + 1050, lzx, 32768);
    module_image[1024 + 32794] = 0x80; module_image[1024 + 32795] = 0x10;
    memcpy(module_image + 1024 + 32796, lzx + 32768, 32784);
    length = 65584;
    put32(module_image + 812, 65582);
    SHA1Digest(module_image + 1024, 65582, module_image + 816);
  }
  for (i = 0; i < 16; ++i) session[i] = (uint8_t)i;
  rounds = rijndaelKeySetupEnc(key_schedule, retail_key, 128);
  rijndaelEncrypt(key_schedule, rounds, session, module_image + 64 + 0x150);
  if (encrypted_mode) {
    rounds = rijndaelKeySetupEnc(key_schedule, session, 128); memset(iv, 0, 16);
    for (i = 0; i < length; i += 16) {
      for (j = 0; j < 16; ++j) block[j] = module_image[1024 + i + j] ^ iv[j];
      rijndaelEncrypt(key_schedule, rounds, block, module_image + 1024 + i);
      memcpy(iv, module_image + 1024 + i, 16);
    }
  }
  return 1024 + length;
}
#endif
int main(void) {
  xe_byte_storage storage = {NULL, 0, 0};
  cipher c;
  uint8_t key[16], iv[16], ciphertext[32], plaintext[32], digest[20], info[24];
  uint8_t encrypted[128], block[16], previous[16];
  u32 encryption_key[4 * (MAXNR + 1)];
  uint32_t bytes, pages, rng = 12345;
  size_t n, i, j;
  unsigned mode, fault;
  CHECK(xe_bytes_resize(&storage, 32)); memset(storage.data, 0x37, 32);
  CHECK(xe_bytes_resize(&storage, 0) && xe_bytes_resize(&storage, 32) && allocations == 1);
  CHECK(storage.data[31] == 0x37);
  fail_allocation = 1; CHECK(!xe_bytes_resize(&storage, 128));
  CHECK(storage.size == 32 && storage.capacity == 32 && storage.data[31] == 0x37);
  fail_allocation = 0; CHECK(xe_bytes_resize_zero(&storage, 64));
  for (i = 0; i < 32; ++i) CHECK(storage.data[i] == 0x37 && storage.data[32 + i] == 0);
  xe_bytes_free(&storage); xe_bytes_free(&storage); CHECK(frees == 1 && !storage.data);
  for (mode = 0; mode < 3; ++mode) {
    make_header(mode); CHECK(xe_xex_header_valid(header, sizeof(header), &bytes) && bytes == sizeof(header));
    header[807] = 3; CHECK(xe_xex_header_valid(header, sizeof(header), NULL));
    for (fault = 0; fault < 7; ++fault) {
      make_header(mode);
      switch (fault) {
        case 0: put32(header + 8, 1025); break;
        case 1: put32(header + 20, 9999); break;
        case 2: put32(header + 16, 1000); break;
        case 3: put32(header + 28, 1001); break;
        case 4: put32(header + 800, 4096); break;
        case 5: put32(header + 64 + (mode == 2 ? 0x184 : mode == 1 ? 0x168 : 0x154) - 4, 0xffffffffU); break;
        default: header[807] = 4; break;
      }
      CHECK(!xe_xex_header_valid(header, sizeof(header), NULL));
    }
  }
  make_header(2);
  for (i = 0; i < sizeof(header); ++i) CHECK(!xe_xex_header_valid(header, i, NULL));
  CHECK(xe_xex_page_bytes(header + 64 + 0x184, 1, 65536, 65536, &pages) && pages == 65536);
  CHECK(!xe_xex_page_bytes(header + 64 + 0x184, 1, 65536, 65535, &pages));
  /* NIST SP800-38A F.2.2 first two AES128-CBC blocks. */
  hex(key, "2b7e151628aed2a6abf7158809cf4f3c");
  hex(iv, "000102030405060708090a0b0c0d0e0f");
  hex(ciphertext, "7649abac8119b246cee98e9b12e9197d5086cb9b507219ee95db113a917678b2");
  hex(plaintext, "6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e51");
  c.rounds = rijndaelKeySetupDec(c.key, key, 128); memcpy(c.iv, iv, 16);
  CHECK(xe_xex_cbc(&c, decrypt, c.iv, ciphertext, 32, output, sizeof(output)) && !memcmp(output, plaintext, 32));
  memcpy(workspace, ciphertext, 32); memcpy(c.iv, iv, 16);
  CHECK(xe_xex_cbc(&c, decrypt, c.iv, workspace, 32, workspace, sizeof(workspace)) && !memcmp(workspace, plaintext, 32));
  memcpy(c.iv, iv, 16);
  CHECK(decrypt_span(&c, ciphertext, output, 16) && decrypt_span(&c, ciphertext + 16, output + 16, 16));
  CHECK(!memcmp(output, plaintext, 32));
  CHECK(!xe_xex_cbc(&c, decrypt, c.iv, ciphertext, 31, output, 128));
  CHECK(!xe_xex_cbc(&c, decrypt, c.iv, workspace, 32, workspace + 1, 64));
  CHECK(xe_xex_overlap(workspace, 32, workspace + 31, 16));
  CHECK(!xe_xex_overlap(workspace, 32, workspace + 32, 16));
  CHECK(!xe_xex_overlap(workspace, 0, workspace, 16));
  make_chain();
  CHECK(xe_xex_deblock(chain, 128, 64, first_hash, hash, NULL, output, 128, &n));
  CHECK(n == 24 && !memcmp(output, "hello worldgoodbye world", 24));
  memcpy(workspace, chain, 128);
  CHECK(xe_xex_deblock(workspace, 128, 64, first_hash, hash, NULL, workspace, 128, &n));
  CHECK(n == 24 && !memcmp(output, workspace, n));
  /* Entire encrypted -> in-place compacted pipeline, same CBC key schedule. */
  rijndaelKeySetupEnc(encryption_key, key, 128); memset(previous, 0, 16);
  for (i = 0; i < 128; i += 16) {
    for (j = 0; j < 16; ++j) block[j] = chain[i + j] ^ previous[j];
    rijndaelEncrypt(encryption_key, c.rounds, block, encrypted + i);
    memcpy(previous, encrypted + i, 16);
  }
  memset(c.iv, 0, 16);
  CHECK(xe_xex_cbc(&c, decrypt, c.iv, encrypted, 128, workspace, 128));
  CHECK(xe_xex_deblock(workspace, 128, 64, first_hash, hash, NULL, workspace, 128, &n) && n == 24);
  CHECK(!memcmp(workspace, output, n));
  for (fault = 0; fault < 7; ++fault) {
    make_chain();
    switch (fault) {
      case 0: chain[90] ^= 1; break;
      case 1: put32(chain, 0xffffffffU); SHA1Digest(chain, 64, first_hash); break;
      case 2: chain[24] = chain[25] = 255; SHA1Digest(chain, 64, first_hash); break;
      case 3: chain[25] = 38; SHA1Digest(chain, 64, first_hash); break;
      default: break;
    }
    CHECK(!xe_xex_deblock(chain, fault == 4 ? 127 : 128, fault == 5 ? 25 : 64,
        first_hash, hash, NULL, output, fault == 6 ? 12 : 128, &n)); CHECK(n == 0);
  }
  memset(info, 0, sizeof(info)); put32(info, 24); info[7] = 1;
  put32(info + 8, 11); put32(info + 12, 7); put32(info + 16, 13); put32(info + 20, 3);
  memcpy(workspace, "hello worldgoodbye world", 24);
  memset(output, 0xa5, sizeof(output));
  CHECK(xe_xex_basic_copy(info, 24, workspace, 24, output, 64, NULL, NULL));
  CHECK(!memcmp(output, workspace, 11) && !memcmp(output + 18, workspace + 11, 13));
  for (i = 11; i < 18; ++i) CHECK(output[i] == 0);
  for (i = 31; i < 64; ++i) CHECK(output[i] == 0);
  memset(output, 0xa5, sizeof(output)); put32(info + 16, 1000);
  CHECK(!xe_xex_basic_copy(info, 24, workspace, 24, output, 64, NULL, NULL));
  CHECK(output[0] == 0xa5 && output[63] == 0xa5);
  /* Bounds fuzzing with real SHA1, always private untrusted output. */
  for (i = 0; i < 10000; ++i) {
    make_chain(); rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    chain[rng % 128] ^= (uint8_t)(rng >> 24);
    SHA1Digest(chain, 64, digest);
    (void)xe_xex_deblock(chain, 128, 64, digest, hash, NULL, output, 128, &n);
    CHECK(n <= 128);
  }
#ifdef XE_XEX_BRIDGE
  for (mode = 0; mode < 3; ++mode) for (fault = 0; fault < 2; ++fault) {
    n = make_module(mode, (int)fault);
    CHECK(xe_test_xex_module(module_image, n, expected_image, 65536, 1));
  }
  n = make_module(2, 1); module_image[n - 30] ^= 1;
  CHECK(xe_test_xex_module(module_image, n, expected_image, 65536, 0));
  n = make_module(0, 0); put32(module_image + 4, 0x10);
  CHECK(xe_test_xex_patch_without_descriptor(module_image, n));
  puts("Actual XEX modules: six compression/encryption images match guest bytes; corrupt encrypted chain rejected");
#endif
  puts("XEX: owned C89 bytes, header/security bounds, CBC vectors, basic holes and in-place hash-checked chunks pass");
  return 0;
}
