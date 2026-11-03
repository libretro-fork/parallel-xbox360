/* Rzstd compatibility and bounded output; MSVC C89. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <encodings/rzstd.h>
#include "rzstd_legacy_frame.h"
static unsigned char source[200003], packed[240000], decoded[200003];
static int legacy_test(void) {
  size_t n, wrote;
  int status;
  for (n=0;n<sizeof(source);++n) source[n]=(unsigned char)((n%251)^(n/32768));
  status=rzstd_decode(decoded,sizeof(decoded),rzstd_legacy_frame,sizeof(rzstd_legacy_frame),&wrote);
  if(status!=RZSTD_PROCESS_END||wrote!=sizeof(source)||memcmp(source,decoded,wrote))return 0;
  memcpy(packed,rzstd_legacy_frame,sizeof(rzstd_legacy_frame));
  packed[sizeof(rzstd_legacy_frame)-1]^=1;
  if(rzstd_decode(decoded,sizeof(decoded),packed,sizeof(rzstd_legacy_frame),&wrote)!=RZSTD_PROCESS_ERROR)return 0;
  for(n=1;n<=16;++n)
    if(rzstd_decode(decoded,sizeof(decoded),rzstd_legacy_frame,sizeof(rzstd_legacy_frame)-n,&wrote)!=RZSTD_PROCESS_ERROR)return 0;
  return 1;
}
static int bounded_tails(void)
{
   static const size_t sizes[] = { 17, 31, 32, 33, 63, 64, 65, 257, 65537 };
   size_t cap = rzstd_compress_bound(65537), i, j, n, len, got;
   uint8_t *src = (uint8_t*)malloc(65537);
   uint8_t *z   = (uint8_t*)malloc(cap);
   uint8_t *out = (uint8_t*)malloc(65537);
   rzstd_decoder_t *decoder = rzstd_decoder_new();
   unsigned period;
   int ok = 0;

   if (!src || !z || !out || !decoder)
      goto done;
   for (period = 1; period <= 32; period++)
   {
      for (i = 0; i < 65537; i++)
         src[i] = (uint8_t)((i % period) * 13 + 7);
      for (j = 0; j < sizeof(sizes) / sizeof(sizes[0]); j++)
      {
         n = sizes[j];
         memset(out, 0xa5, 65537);
         if (rzstd_encode(z, cap, src, n, 3, &len) != RZSTD_PROCESS_END
               || rzstd_decode_reuse(decoder, out, n, z, len, &got)
                  != RZSTD_PROCESS_END
               || got != n || memcmp(src, out, n)
               || (n < 65537 && out[n] != 0xa5))
            goto done;
      }
   }
   ok = 1;
done:
   rzstd_decoder_free(decoder);
   free(src);
   free(z);
   free(out);
   return ok;
}

int main(void) {
  static const size_t sizes[]={0,1,31,255,256,65535,65536,131072,200003};
  unsigned mode, level;
  size_t i,j,n,length,wrote;
  int status;
  rzstd_decoder_t *decoder = rzstd_decoder_new();
  if (!decoder) return 7;
  if(!legacy_test())return 1;
  if(!bounded_tails())return 9;
  for(mode=0;mode<4;++mode) {
    for(i=0;i<sizeof(source);++i)
      source[i]=(unsigned char)(mode==0?0:mode==1?i%251:mode==2?(i*131u)^(i>>7):((i%113)?'a':i));
    for(j=0;j<sizeof(sizes)/sizeof(sizes[0]);++j) {
      n=sizes[j];
      for(level=1;level<=9;level+=2) {
        if(rzstd_compress_bound(n)>sizeof(packed))return 2;
        status=rzstd_encode(packed,sizeof(packed)-1,source,n,(int)level,&length);
        if(status!=RZSTD_PROCESS_END||length>=sizeof(packed))return 3;
        status=rzstd_decode_reuse(decoder,decoded,n,packed,length,&wrote);
        if(status!=RZSTD_PROCESS_END||wrote!=n||memcmp(source,decoded,n))return 4;
        if(n&&rzstd_decode_reuse(decoder,decoded,n-1,packed,length,&wrote)!=RZSTD_PROCESS_ERROR)return 5;
        packed[length]=0;
        if(rzstd_decode_reuse(decoder,decoded,n,packed,length+1,&wrote)!=RZSTD_PROCESS_ERROR)return 6;
        if(rzstd_decode_reuse(decoder,decoded,n,packed,length,&wrote)!=RZSTD_PROCESS_END ||
            wrote!=n || memcmp(source,decoded,n)) return 8;
      }
    }
  }
  rzstd_decoder_free(decoder);
  puts("PASS: legacy multiblock/checksum fixture, 180 encoded frames, reused scratch, recovery and bounds");
  return 0;
}
