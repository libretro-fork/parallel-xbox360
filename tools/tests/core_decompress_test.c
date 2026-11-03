/* Complete-stream error handling and ownership; MSVC C89. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <encodings/deflate.h>
#include "core_decompress.h"
static unsigned char source[200003], packed[220000], restored[200004];
/* Independent RFC-compatible fixtures, not produced by rdeflate. */
static const unsigned char hello_zlib[] = {
  0x78,0x9c,0xcb,0x48,0xcd,0xc9,0xc9,0x07,0x00,0x06,0x2c,0x02,0x15
};
static const unsigned char hello_gzip[] = {
  0x1f,0x8b,0x08,0,0,0,0,0,0,3,0xcb,0x48,0xcd,0xc9,0xc9,7,0,
  0x86,0xa6,0x10,0x36,5,0,0,0
};
static const unsigned char empty_zlib[] = {0x78,0x9c,3,0,0,0,0,1};
static int check(int bits, int level) {
  void *stream;
  size_t read, wrote, n, length;
  uint8_t *allocated;
  int status;
  stream = rdeflate_new(level, bits); if (!stream) return 0;
  rdeflate_set_in(stream, source, sizeof(source));
  rdeflate_set_out(stream, packed, sizeof(packed)); rdeflate_finish(stream);
  status = rdeflate_process(stream, &read, &wrote); rdeflate_free(stream);
  if (status != RDEFLATE_PROCESS_END) return 0;
  if (!xe_decompress_exact(restored, sizeof(source), packed, wrote, bits) ||
      memcmp(source, restored, sizeof(source))) return 0;
  if (xe_decompress_exact(restored, sizeof(source)-1, packed, wrote, bits) ||
      xe_decompress_exact(restored, sizeof(source)+1, packed, wrote, bits)) return 0;
  if (!xe_decompress_alloc(&allocated, &length, packed, wrote, bits, 1, sizeof(source)) ||
      length != sizeof(source) || memcmp(source, allocated, length)) return 0;
  free(allocated);
  if (xe_decompress_alloc(&allocated, &length, packed, wrote, bits, 1, sizeof(source)-1) ||
      allocated || length) return 0;
  for (n = 1; n <= 16 && n < wrote; ++n) {
    if (xe_decompress_alloc(&allocated, &length, packed, wrote-n, bits, 1024, sizeof(source)+1) ||
        allocated || length) return 0;
  }
  if (bits > 0) {
    packed[wrote-1] ^= 1;
    if (xe_decompress_exact(restored, sizeof(source), packed, wrote, bits)) return 0;
  }
  return 1;
}
int main(void) {
  size_t i; int level;
  if (!xe_decompress_exact(restored, 5, hello_zlib, sizeof(hello_zlib), 15) ||
      memcmp(restored, "hello", 5) ||
      !xe_decompress_exact(restored, 5, hello_gzip, sizeof(hello_gzip), 31) ||
      memcmp(restored, "hello", 5) ||
      !xe_decompress_exact(restored, 5, hello_zlib + 2, 7, -15) ||
      memcmp(restored, "hello", 5) ||
      !xe_decompress_exact(NULL, 0, empty_zlib, sizeof(empty_zlib), 15)) return 2;
  for (i=0;i<sizeof(source);++i) source[i]=(unsigned char)((i%251) ^ (i/32768));
  for (level=0;level<=9;level+=3)
    if (!check(-15,level)||!check(15,level)||!check(31,level)) {
      fprintf(stderr,"FAIL: decompression level %d\n",level); return 1;
    }
  puts("PASS: raw/zlib/gzip, exact output, realloc/history, size limits, truncation and corrupt trailers");
  return 0;
}
