#include "libretro/core_guest_hash.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void xe_reference_md5(uint8_t *,const uint8_t *,size_t,uint8_t *,size_t,int);
void xe_reference_sha512(uint8_t *,const uint8_t *,size_t,uint8_t *,size_t,int);
/* The reference uses stack contexts. Any accidental allocator use fails. */
void *av_mallocz(size_t size) { (void)size; abort(); }
typedef int (*Init)(uint8_t *);
typedef int (*Update)(uint8_t *,const uint8_t *,size_t);
typedef int (*Final)(uint8_t *,uint8_t *,size_t);
typedef void (*Reference)(uint8_t *,const uint8_t *,size_t,uint8_t *,size_t,int);
static uint8_t data[65538],current[384],old[384],saved[384];
static void put64(uint8_t *p,uint64_t count) {
  unsigned i;
  for(i=0;i<8;++i) p[i]=(uint8_t)(count>>(56-8*i));
}
static void start(Init init,Reference ref) {
  memset(current,0xa7,sizeof(current)); memcpy(old,current,sizeof(old));
  assert(init(current+7)); ref(old+7,NULL,0,NULL,0,0);
  assert(!memcmp(current,old,sizeof(old)));
}
static void finals(Final final,Update update,Reference ref,unsigned digest) {
  static const unsigned offsets[]={0,8,24,72,130,210};
  uint8_t a[80],b[80];
  unsigned cap,i;
  memcpy(saved,current,sizeof(saved));
  for(cap=0;cap<=digest+8;++cap) {
    memcpy(current,saved,sizeof(saved)); memcpy(old,saved,sizeof(saved));
    memset(a,0x4c,sizeof(a)); memcpy(b,a,sizeof(b));
    assert(final(current+7,a+1,cap)); ref(old+7,NULL,0,b+1,cap,2);
    assert(!memcmp(a,b,sizeof(a))); assert(!memcmp(current,old,sizeof(old)));
  }
  for(i=0;i<sizeof(offsets)/sizeof(offsets[0]);++i) {
    memcpy(current,saved,sizeof(saved)); memcpy(old,saved,sizeof(saved));
    assert(final(current+7,current+offsets[i],digest));
    ref(old+7,NULL,0,old+offsets[i],digest,2);
    assert(!memcmp(current,old,sizeof(old)));
  }
  memcpy(current,saved,sizeof(saved)); memcpy(old,saved,sizeof(saved));
  assert(final(current+7,a,digest)); ref(old+7,NULL,0,b,digest,2);
  assert(final(current+7,a,digest)); ref(old+7,NULL,0,b,digest,2);
  assert(!memcmp(a,b,digest)); assert(!memcmp(current,old,sizeof(old)));
  assert(update(current+7,data+1,200)); ref(old+7,data+1,200,NULL,0,1);
  assert(!memcmp(current,old,sizeof(old)));
}
static void known(Init init,Update update,Final final,const char *hex,unsigned size) {
  static const char digits[]="0123456789abcdef";
  uint8_t out[64];
  unsigned i;
  assert(init(current+7)); assert(update(current+7,(const uint8_t *)"abc",3));
  assert(final(current+7,out,size));
  for(i=0;i<size;++i) { assert(hex[i*2]==digits[out[i]>>4]); assert(hex[i*2+1]==digits[out[i]&15]); }
}
static void exercise(Init init,Update update,Final final,Reference ref,unsigned digest) {
  static const unsigned chunks[]={1,3,63,64,127,128,129,4096};
  static const uint64_t counts[]={0x1ffffff0U,0xfffffff0U,
    ((uint64_t)0x1fffffffU<<32)|0xfffffff0U,
    ((uint64_t)0xffffffffU<<32)|0xfffffff0U,~(uint64_t)0};
  size_t length,at,size;
  unsigned test,chunk,offset;
  for(test=0;test<312;++test) {
    length=test<300?test:(size_t)(test-299)*5000;
    for(chunk=0;chunk<sizeof(chunks)/sizeof(chunks[0]);++chunk) {
      start(init,ref);
      for(at=0;at<length;at+=size) {
        size=length-at; if(size>chunks[chunk]) size=chunks[chunk];
        assert(update(current+7,data+1+at,size)); ref(old+7,data+1+at,size,NULL,0,1);
        assert(!memcmp(current,old,sizeof(old)));
      }
      if(chunk==3) finals(final,update,ref,digest);
    }
  }
  for(test=0;test<sizeof(counts)/sizeof(counts[0]);++test) {
    start(init,ref); put64(current+7,counts[test]);
    memcpy(current+31,data,64); if(digest==64) memcpy(current+79,data,128);
    memcpy(old,current,sizeof(old));
    assert(update(current+7,data+1,200)); ref(old+7,data+1,200,NULL,0,1);
    assert(!memcmp(current,old,sizeof(old))); finals(final,update,ref,digest);
  }
  for(offset=0;offset<208;++offset) {
    start(init,ref); put64(current+7,offset%64); memcpy(old,current,sizeof(old));
    assert(update(current+7,current+offset,140)); ref(old+7,old+offset,140,NULL,0,1);
    assert(!memcmp(current,old,sizeof(old)));
  }
  memcpy(saved,current,sizeof(saved));
  assert(!update(current+7,NULL,1)); assert(!final(current+7,NULL,1));
  assert(!memcmp(current,saved,sizeof(saved)));
  assert(update(current+7,NULL,0)); assert(!init(NULL));
}
static void large_md5(void) {
  size_t i,length=(size_t)0x10000000U+79;
  uint8_t *input=(uint8_t *)malloc(length+1);
  assert(input);
  for(i=0;i<=length;++i) input[i]=(uint8_t)(i*71+(i>>6)+(i>>24));
  start(xe_guest_md5_init,xe_reference_md5);
  assert(xe_guest_md5_update(current+7,data+1,5));
  xe_reference_md5(old+7,data+1,5,NULL,0,1);
  assert(xe_guest_md5_update(current+7,input+1,length));
  xe_reference_md5(old+7,input+1,length,NULL,0,1);
  assert(!memcmp(current,old,sizeof(old)));
  finals(xe_guest_md5_final,xe_guest_md5_update,xe_reference_md5,16);
  free(input);
  puts("guest MD5 >256 MiB chunk boundary and retained buffer: PASS");
}
int main(int argc,char **argv) {
  size_t i;
  for(i=0;i<sizeof(data);++i) data[i]=(uint8_t)(i*71+(i>>3));
  known(xe_guest_md5_init,xe_guest_md5_update,xe_guest_md5_final,
    "900150983cd24fb0d6963f7d28e17f72",16);
  known(xe_guest_sha512_init,xe_guest_sha512_update,xe_guest_sha512_final,
    "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
    "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f",64);
  exercise(xe_guest_md5_init,xe_guest_md5_update,xe_guest_md5_final,xe_reference_md5,16);
  exercise(xe_guest_sha512_init,xe_guest_sha512_update,xe_guest_sha512_final,xe_reference_sha512,64);
  if(argc>1) { assert(argc==2 && !strcmp(argv[1],"--large")); large_md5(); }
  puts("guest MD5/SHA-512 exact state, known answers, aliases and count wrap: PASS");
  return 0;
}
