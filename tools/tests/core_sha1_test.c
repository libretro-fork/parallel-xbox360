/* Compare host hash replacement against the previous implementation. */
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <lrc_hash.h>
void xe_test_legacy_sha1(const unsigned char *, size_t, unsigned char *);
int main(void) {
  uint8_t *data, old[20], current[20], streamed[20];
  struct sha1_state state;
  uint32_t random = 0x12345678U;
  size_t i, length, at, count;
  unsigned test;
  data = (uint8_t *)malloc(1048577);
  if (!data) return 1;
  for (i = 0; i < 1048577; ++i) {
    random ^= random << 13; random ^= random >> 17; random ^= random << 5;
    data[i] = (uint8_t)random;
  }
  for (test = 0; test < 1100; ++test) {
    length = test < 1024 ? test : (test - 1023) * 13797U;
    if (length > 1048576) length = 1048576;
    xe_test_legacy_sha1(data + 1, length, old);
    SHA1Digest(data + 1, length, current);
    sha1_stream_init(&state);
    for (at = 0; at < length; at += count) {
      count = length - at;
      if (count > 61) count = 61;
      sha1_stream_update(&state, data + 1 + at, count);
    }
    if (!sha1_stream_final(&state, streamed) || memcmp(old, current, 20) ||
        memcmp(old, streamed, 20)) {
      fprintf(stderr, "SHA-1 mismatch at %lu bytes\n", (unsigned long)length);
      free(data); return 2;
    }
  }
  free(data);
  puts("host SHA-1 legacy/common/stream differential: PASS");
  return 0;
}
