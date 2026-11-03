/* Zstandard checksum migration regression; MSVC C89. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <zstd.h>
#include <encodings/xxh64.h>
static unsigned char source[200003], packed[220000], restored[200003];
int main(void) {
  ZSTD_CCtx *ctx;
  size_t n, size, result;
  uint64_t checksum;
  unsigned encoded;
  for(n=0;n<sizeof(source);++n)source[n]=(unsigned char)((n%251)^(n/32768));
  ctx=ZSTD_createCCtx();if(!ctx)return 2;
  result=ZSTD_CCtx_setParameter(ctx,ZSTD_c_checksumFlag,1);
  if(ZSTD_isError(result))return 3;
  size=ZSTD_compress2(ctx,packed,sizeof(packed),source,sizeof(source));
  ZSTD_freeCCtx(ctx);if(ZSTD_isError(size)||size<4)return 4;
  checksum=xxh64(source,sizeof(source),0);
  encoded=(unsigned)packed[size-4]|((unsigned)packed[size-3]<<8)|
      ((unsigned)packed[size-2]<<16)|((unsigned)packed[size-1]<<24);
  if(encoded!=(uint32_t)checksum)return 5;
  result=ZSTD_decompress(restored,sizeof(restored),packed,size);
  if(result!=sizeof(source)||memcmp(source,restored,sizeof(source)))return 6;
  packed[size-1]^=1;
  result=ZSTD_decompress(restored,sizeof(restored),packed,size);
  if(!ZSTD_isError(result))return 7;
  puts("PASS: Zstandard checksum-bearing frame, XXH64 trailer and corrupt checksum rejection");return 0;
}
