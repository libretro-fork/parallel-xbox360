#include "core_file_span.h"
#include <stdint.h>
#include <stdio.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %u\n",(unsigned)__LINE__); return 1; } } while (0)
int main(void) {
   unsigned char source[128], destination[130], original[128];
   size_t totals[4] = {0,1,7,64};
   size_t t, base, offset, length, absolute, i, n;
   int valid;
   for (i=0;i<sizeof(source);++i) source[i]=(unsigned char)(i*37);
   for(t=0;t<4;++t) for(base=0;base<72;++base) for(offset=0;offset<72;++offset) for(length=0;length<72;++length) {
      valid = (uint64_t)base + offset + length <= totals[t];
      absolute=999; memset(destination,0xAD,sizeof(destination));
      CHECK(xe_file_span_range(totals[t],base,offset,length,&absolute)==valid);
      CHECK(absolute==(valid ? base+offset : 999));
      CHECK(xe_file_span_read(source,totals[t],base,offset,destination+1,length)==valid);
      CHECK(destination[0]==0xAD);
      for(i=0;i<sizeof(destination)-1;++i)
         CHECK(destination[i+1]==(valid && i<length ? source[base+offset+i] : 0xAD));
   }
   CHECK(!xe_file_span_range(SIZE_MAX,SIZE_MAX-8,16,0,NULL));
   CHECK(!xe_file_span_range(SIZE_MAX,SIZE_MAX-4,2,4,NULL));
   CHECK(xe_file_span_read(NULL,0,0,0,NULL,0));
   CHECK(!xe_file_span_read(NULL,1,0,0,destination,1));
   CHECK(!xe_file_span_read(source,1,0,0,NULL,1));
   memcpy(original,source,sizeof(source));
   CHECK(xe_file_span_read(source,sizeof(source),0,0,source+3,30));
   CHECK(!memcmp(source+3,original,30));
   for(base=0;base<12;++base) for(offset=0;offset<20;++offset) for(length=0;length<20;++length) {
      n=offset<=16 ? 16-offset : 0;
      if(length && length<n) n=length;
      valid=offset<=16 && base+offset+n<=24;
      absolute=999; i=999;
      CHECK(xe_file_span_slice(24,base,16,offset,length,&absolute,&i)==valid);
      CHECK(absolute==(valid?base+offset:999) && i==(valid?n:999));
   }
   CHECK(!xe_file_span_slice(SIZE_MAX,SIZE_MAX-4,16,8,1,NULL,NULL));
   puts("PASS: mapped copy/slice bounds, overflow, guards, overlap and logical tails");
   return 0;
}
