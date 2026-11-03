/* Component host-timing measurement; both variants consume identical bytes. */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h>
#ifdef XE_SHA_TEST_SCALAR
uint64_t xe_test_cpu_features_get(void) { return 0; }
#endif
#ifdef XE_SHA_BASELINE
#include "sha_256.h"
#else
#include <lrc_hash.h>
#endif
int main(int argc, char **argv) {
  uint8_t *data, digest[32];
  size_t chunk, total, at, n, i;
  clock_t start, end;
#ifdef XE_SHA_BASELINE
  struct Sha_256 state;
#else
  struct sha256_state state;
#endif
  if (argc != 3) return 1;
  chunk = (size_t)strtoul(argv[1], NULL, 10);
  total = (size_t)strtoul(argv[2], NULL, 10);
  if (!chunk || chunk > 16U * 1024 * 1024 || !total) return 1;
  data = (uint8_t *)malloc(chunk);
  if (!data) return 1;
  for (i = 0; i < chunk; ++i) data[i] = (uint8_t)(i * 37 + 9);
  start = clock();
#ifdef XE_SHA_BASELINE
  sha_256_init(&state, digest);
#else
  sha256_stream_init(&state, 0);
#endif
  for (at = 0; at < total; at += n) {
    n = total - at; if (n > chunk) n = chunk;
#ifdef XE_SHA_BASELINE
    sha_256_write(&state, data, n);
#else
    sha256_stream_update(&state, data, n);
#endif
  }
#ifdef XE_SHA_BASELINE
  sha_256_close(&state);
#else
  sha256_stream_final(&state, digest);
#endif
  end = clock();
  printf("%lu,%lu,%.6f,", (unsigned long)chunk, (unsigned long)total,
      (double)(end - start) / CLOCKS_PER_SEC);
  for (i = 0; i < sizeof(digest); ++i) printf("%02x", digest[i]);
  puts("");
  free(data);
  return 0;
}
