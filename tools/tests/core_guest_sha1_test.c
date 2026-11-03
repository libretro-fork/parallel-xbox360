#include "libretro/core_guest_hash.h"
#include "libretro/core_crypto.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void xe_reference_sha1(uint8_t *,const uint8_t *,size_t,uint8_t *,size_t,int);
void xe_reference_hmac_sha1_init(uint8_t *,const uint8_t *,size_t);
void xe_reference_hmac_sha1_final(uint8_t *,uint8_t *,size_t);
void xe_test_legacy_guest_sha1(uint32_t *,uint8_t *,uint32_t *,const uint8_t *,size_t,uint8_t *,int);
void *av_mallocz(size_t size) { (void)size; abort(); }
static uint32_t be32(const uint8_t *p) {
  return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}
static void put32(uint8_t *p,uint32_t v) {
  p[0]=(uint8_t)(v>>24); p[1]=(uint8_t)(v>>16); p[2]=(uint8_t)(v>>8); p[3]=(uint8_t)v;
}
static void legacy(uint8_t *guest,const uint8_t *input,size_t size,
    uint8_t *out,size_t cap,int final) {
  uint32_t words[5],count=be32(guest);
  uint8_t buffer[64],digest[20];
  unsigned i;
  for(i=0;i<5;++i) words[i]=be32(guest+4+i*4);
  memcpy(buffer,guest+24,64);
  xe_test_legacy_guest_sha1(words,buffer,&count,input,size,digest,final);
  if(final && cap) memcpy(out,digest,cap<20?cap:20);
  for(i=0;i<5;++i) put32(guest+4+i*4,words[i]);
  if(final) memcpy(guest+24,buffer,64);
  else {
    put32(guest,count);
    if(count&63) memcpy(guest+24,buffer,count&63);
  }
}
static uint8_t data[65537], current[288], old[288], saved[288], prior[288];
static void check_final(void) {
  static const unsigned caps[] = {0,1,15,19,20,21,28};
  static const unsigned offsets[] = {0,4,12,24,52,82};
  uint8_t a[64], b[64];
  unsigned i, j;
  memcpy(saved, current, sizeof(saved));
  if (be32(saved + 7) < 0x20000000U) {
    memcpy(prior, saved, sizeof(prior)); memcpy(old, saved, sizeof(old));
    legacy(prior + 7, NULL, 0, a, 20, 1);
    xe_reference_sha1(old + 7, NULL, 0, b, 20, 2);
    assert(!memcmp(a, b, 20) && !memcmp(prior, old, sizeof(old)));
  }
  for (i = 0; i < sizeof(caps) / sizeof(caps[0]); ++i) {
    memcpy(current, saved, sizeof(saved)); memcpy(old, saved, sizeof(saved));
    memset(a, 0xa7, sizeof(a)); memset(b, 0xa7, sizeof(b));
    xe_reference_sha1(old + 7, NULL, 0, b + 3, caps[i], 2);
    assert(xe_guest_sha1_final(current + 7, a + 3, caps[i]));
    assert(!memcmp(a, b, sizeof(a)) && !memcmp(current, old, sizeof(old)));
    /* Finalization and further updates remain guest-visible, even repeated. */
    xe_reference_sha1(old + 7, NULL, 0, b + 3, caps[i], 2);
    assert(xe_guest_sha1_final(current + 7, a + 3, caps[i]));
    assert(!memcmp(a, b, sizeof(a)) && !memcmp(current, old, sizeof(old)));
    xe_reference_sha1(old + 7, data, 5, NULL, 0, 1);
    assert(xe_guest_sha1_update(current + 7, data, 5));
    assert(!memcmp(current, old, sizeof(old)));
    for (j = 0; j < sizeof(offsets) / sizeof(offsets[0]); ++j) {
      memcpy(current, saved, sizeof(saved)); memcpy(old, saved, sizeof(saved));
      xe_reference_sha1(old + 7, NULL, 0, old + 7 + offsets[j], caps[i], 2);
      assert(xe_guest_sha1_final(current + 7, current + 7 + offsets[j], caps[i]));
      assert(!memcmp(current, old, sizeof(old)));
    }
  }
}
static void hmac_vectors(void) {
  static const char *answers[]={
    "b617318655057264e28bc0b6fb378c8ef146be00",
    "effcdf6ae5eb2fa2d27416d5f184df9c259a7c79"
  };
  static const char digits[]="0123456789abcdef";
  const uint8_t *message;
  uint8_t key[160],a[32],b[32];
  unsigned test,i,size,cap,offset;
  for(test=0;test<2;++test) {
    size=test?4:20; memset(key,0x0b,size); if(test) memcpy(key,"Jefe",4);
    message=(const uint8_t *)(test?"what do ya want for nothing?":"Hi There");
    assert(xe_guest_hmac_sha1_init(current+7,key,size));
    assert(xe_guest_sha1_update(current+7,message,strlen((const char *)message)));
    assert(xe_guest_hmac_sha1_final(current+7,a,20));
    for(i=0;i<20;++i) { assert(answers[test][2*i]==digits[a[i]>>4]); assert(answers[test][2*i+1]==digits[a[i]&15]); }
  }
  for(size=0;size<=sizeof(key);++size) {
    memcpy(key,data,size);
    memset(current,0xa7,sizeof(current)); memcpy(old,current,sizeof(old));
    assert(xe_guest_hmac_sha1_init(current+7,key,size));
    xe_reference_hmac_sha1_init(old+7,key,size);
    assert(!memcmp(current,old,sizeof(old)));
    for(i=0;i<257;i+=17) {
      assert(xe_guest_sha1_update(current+7,data+i,17));
      xe_reference_sha1(old+7,data+i,17,NULL,0,1);
      assert(!memcmp(current,old,sizeof(old)));
    }
    memcpy(saved,current,sizeof(saved));
    for(cap=0;cap<=28;++cap) {
      memcpy(current,saved,sizeof(saved)); memcpy(old,saved,sizeof(saved));
      memset(a,0x4c,sizeof(a)); memcpy(b,a,sizeof(b));
      assert(xe_guest_hmac_sha1_final(current+7,a+1,cap));
      xe_reference_hmac_sha1_final(old+7,b+1,cap);
      assert(!memcmp(a,b,sizeof(a))); assert(!memcmp(current,old,sizeof(old)));
    }
    memcpy(current,saved,sizeof(saved)); memcpy(old,saved,sizeof(saved));
    for(i=0;i<2;++i) {
      assert(xe_guest_hmac_sha1_final(current+7,a,20));
      xe_reference_hmac_sha1_final(old+7,b,20);
      assert(!memcmp(a,b,20) && !memcmp(current,old,sizeof(old)));
    }
    for(offset=0;offset<176;offset+=11) {
      memcpy(current,saved,sizeof(saved)); memcpy(old,saved,sizeof(saved));
      assert(xe_guest_hmac_sha1_final(current+7,current+offset,20));
      xe_reference_hmac_sha1_final(old+7,old+offset,20);
      assert(!memcmp(current,old,sizeof(old)));
    }
  }
  /* Key borrowing must precede clearing the guest state. */
  memcpy(current,data,sizeof(current)); memcpy(old,current,sizeof(old));
  assert(xe_guest_hmac_sha1_init(current+7,current+10,100));
  xe_reference_hmac_sha1_init(old+7,old+10,100);
  assert(!memcmp(current,old,sizeof(old)));
}
static void spans(void) {
  uint8_t a[32],b[32],input[400];
  unsigned split,cap;
  for(split=0;split<=100;++split) {
    memset(old,0xa7,sizeof(old)); xe_reference_sha1(old+7,NULL,0,NULL,0,0);
    xe_reference_sha1(old+7,data+1,split,NULL,0,1);
    xe_reference_sha1(old+7,data+110,100-split,NULL,0,1);
    xe_reference_sha1(old+7,data+220,80,NULL,0,1);
    for(cap=0;cap<=28;++cap) {
      memset(a,0x4c,sizeof(a)); memcpy(b,a,sizeof(b)); memcpy(prior,old,sizeof(old));
      assert(xe_sha1_3(data+1,split,data+110,100-split,data+220,80,a+1,cap));
      xe_reference_sha1(prior+7,NULL,0,b+1,cap,2);
      assert(!memcmp(a,b,sizeof(a)));
    }
  }
  memcpy(input,data,sizeof(input));
  memset(old,0,sizeof(old)); xe_reference_sha1(old+7,NULL,0,NULL,0,0);
  xe_reference_sha1(old+7,input+1,300,NULL,0,1); xe_reference_sha1(old+7,NULL,0,a,20,2);
  assert(xe_sha1_3(input+1,100,input+101,100,input+201,100,input+10,20));
  assert(!memcmp(input+10,a,20));
  memset(a,0x4c,sizeof(a));
  assert(!xe_sha1_3(NULL,1,NULL,0,NULL,0,a,20));
  for(cap=0;cap<sizeof(a);++cap) assert(a[cap]==0x4c);
}
static void length_fix(void) {
  uint8_t a[20],b[20];
  assert(xe_guest_sha1_init(current+7)); put32(current+7,0x20000000U);
  memcpy(old,current,sizeof(old)); memcpy(prior,current,sizeof(prior));
  assert(xe_guest_sha1_final(current+7,a,20));
  xe_reference_sha1(old+7,NULL,0,b,20,2);
  assert(!memcmp(current,old,sizeof(old))); assert(!memcmp(a,b,20));
  legacy(prior+7,NULL,0,b,20,1);
  assert(memcmp(a,b,20));
  assert(be32(current+7+24+56)==1 && be32(current+7+24+60)==0);
}
static void large_message(void) {
  uint8_t *input=(uint8_t *)malloc(1048577),a[20],b[20];
  unsigned i,block;
  assert(input);
  for(i=0;i<1048577;++i) input[i]=(uint8_t)(i*71+(i>>6));
  assert(xe_guest_sha1_init(current+7)); memcpy(old,current,sizeof(old));
  for(block=0;block<513;++block) {
    assert(xe_guest_sha1_update(current+7,input+1,1048576));
    xe_reference_sha1(old+7,input+1,1048576,NULL,0,1);
    assert(!memcmp(current,old,sizeof(old)));
  }
  memcpy(prior,current,sizeof(prior));
  assert(xe_guest_sha1_final(current+7,a,20)); xe_reference_sha1(old+7,NULL,0,b,20,2);
  assert(!memcmp(a,b,20)); assert(!memcmp(current,old,sizeof(old)));
  legacy(prior+7,NULL,0,b,20,1); assert(memcmp(a,b,20));
  free(input);
  puts("513 MiB guest SHA-1 standard length, old digest mismatch: PASS");
}
int main(int argc,char **argv) {
  static const uint8_t abc[20] = {
    0xa9,0x99,0x3e,0x36,0x47,0x06,0x81,0x6a,0xba,0x3e,
    0x25,0x71,0x78,0x50,0xc2,0x6c,0x9c,0xd0,0xd8,0x9d
  };
  static const unsigned chunks[] = {1,3,17,64,65,127,4096};
  static const uint32_t counts[] = {0x1ffffff0U,0xfffffff0U,0xffffffffU};
  uint32_t random = 0x12345678U;
  size_t i, length, at, size;
  unsigned test, chunk, offset;
  assert(xe_guest_sha1_init(current + 7));
  assert(xe_guest_sha1_update(current + 7, (const uint8_t *)"abc", 3));
  assert(xe_guest_sha1_final(current + 7, current + 120, 20));
  assert(!memcmp(current + 120, abc, sizeof(abc)));
  for (i = 0; i < sizeof(data); ++i) {
    random ^= random << 13; random ^= random >> 17; random ^= random << 5;
    data[i] = (uint8_t)random;
  }
  for (test = 0; test < 330; ++test) {
    length = test < 260 ? test : (test - 259) * 907U;
    for (chunk = 0; chunk < sizeof(chunks) / sizeof(chunks[0]); ++chunk) {
      memset(current, 0xa7, sizeof(current));
      assert(xe_guest_sha1_init(current + 7));
      memcpy(old, current, sizeof(old));
      for (at = 0; at < length; at += size) {
        size = length - at; if (size > chunks[chunk]) size = chunks[chunk];
        memcpy(prior, old, sizeof(prior));
        legacy(prior + 7, data + 1 + at, size, NULL, 0, 0);
        xe_reference_sha1(old + 7, data + 1 + at, size, NULL, 0, 1);
        assert(!memcmp(prior, old, sizeof(old)));
        assert(xe_guest_sha1_update(current + 7, data + 1 + at, size));
        assert(!memcmp(current, old, sizeof(old)));
      }
      if (chunk == 3) check_final();
    }
  }
  for (test = 0; test < sizeof(counts) / sizeof(counts[0]); ++test) {
    assert(xe_guest_sha1_init(current + 7));
    put32(current + 7, counts[test]); memcpy(current + 31, data, 64);
    memcpy(old, current, sizeof(old));
    xe_reference_sha1(old + 7, data, 80, NULL, 0, 1);
    assert(xe_guest_sha1_update(current + 7, data, 80));
    assert(!memcmp(current, old, sizeof(old))); check_final();
  }
  for (offset = 0; offset < 100; ++offset) {
    assert(xe_guest_sha1_init(current + 7));
    put32(current + 7, offset % 64); memcpy(current + 31, data, 64);
    memcpy(old, current, sizeof(old));
    xe_reference_sha1(old + 7, old + offset, 17, NULL, 0, 1);
    assert(xe_guest_sha1_update(current + 7, current + offset, 17));
    assert(!memcmp(current, old, sizeof(old)));
  }
  memcpy(saved, current, sizeof(saved));
  assert(!xe_guest_sha1_update(current + 7, NULL, 1));
  assert(!xe_guest_sha1_final(current + 7, NULL, 1));
  assert(!memcmp(saved, current, sizeof(saved)));
  length_fix(); hmac_vectors(); spans();
  if(argc>1) { assert(argc==2 && !strcmp(argv[1],"--large")); large_message(); }
  puts("guest SHA-1 exact state, aliasing, repeated final and count wrap: PASS");
  return 0;
}
