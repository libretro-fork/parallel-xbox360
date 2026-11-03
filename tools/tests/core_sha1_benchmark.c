/* Developer-only cache-hot SHA-1 throughput; host timers never enter the core. */
#include "libretro/core_guest_hash.h"
#include <lrc_hash.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static double now(void) {
  LARGE_INTEGER ticks,freq;
  QueryPerformanceCounter(&ticks); QueryPerformanceFrequency(&freq);
  return (double)ticks.QuadPart/(double)freq.QuadPart;
}
#else
#include <time.h>
static double now(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC,&t);
  return (double)t.tv_sec+(double)t.tv_nsec*0.000000001;
}
#endif
#ifdef XE_SHA_TEST_SCALAR
uint64_t xe_test_cpu_features_get(void) { return 0; }
#endif
void xe_test_legacy_sha1(const uint8_t *,size_t,uint8_t *);
void xe_test_legacy_guest_sha1(uint32_t *,uint8_t *,uint32_t *,const uint8_t *,size_t,uint8_t *,int);
static uint32_t load32(const uint8_t *p) {
  return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}
static void store32(uint8_t *p,uint32_t v) {
  p[0]=(uint8_t)(v>>24); p[1]=(uint8_t)(v>>16); p[2]=(uint8_t)(v>>8); p[3]=(uint8_t)v;
}
static void legacy_guest(uint8_t *guest,const uint8_t *input,size_t size,uint8_t *out,int final) {
  uint32_t words[5],count=load32(guest);
  uint8_t buffer[64];
  unsigned i;
  for(i=0;i<5;++i) words[i]=load32(guest+4+i*4);
  memcpy(buffer,guest+24,64);
  xe_test_legacy_guest_sha1(words,buffer,&count,input,size,out,final);
  for(i=0;i<5;++i) store32(guest+4+i*4,words[i]);
  if(final) memcpy(guest+24,buffer,64);
  else { store32(guest,count); if(count&63) memcpy(guest+24,buffer,count&63); }
}
int main(int argc,char **argv) {
  static const uint32_t iv[5]={0x67452301,0xefcdab89,0x98badcfe,0x10325476,0xc3d2e1f0};
  uint8_t guest[88],digest[20],expected[20],*input;
  size_t size,i;
  unsigned long iterations,at;
  unsigned mode,j;
  volatile uint32_t checksum=0;
  double start,seconds;
  if(argc!=4) return 2;
  mode=(unsigned)strtoul(argv[1],NULL,10); size=(size_t)strtoul(argv[2],NULL,10);
  iterations=strtoul(argv[3],NULL,10);
  if(mode>3 || !size || size>1048576 || !iterations) return 2;
  input=(uint8_t *)malloc(size+1); if(!input) return 3;
  for(i=0;i<=size;++i) input[i]=(uint8_t)(i*71+(i>>6));
  SHA1Digest(input+1,size,expected);
  start=now();
  for(at=0;at<iterations;++at) {
    if(mode==0) xe_test_legacy_sha1(input+1,size,digest);
    else if(mode==1) SHA1Digest(input+1,size,digest);
    else if(mode==2) {
      memset(guest,0,sizeof(guest));
      for(j=0;j<5;++j) store32(guest+4+j*4,iv[j]);
      legacy_guest(guest,input+1,size,NULL,0); legacy_guest(guest,NULL,0,digest,1);
    } else {
      if(!xe_guest_sha1_init(guest) || !xe_guest_sha1_update(guest,input+1,size) ||
          !xe_guest_sha1_final(guest,digest,20)) return 4;
    }
    checksum+=digest[at%20];
  }
  seconds=now()-start;
  if(memcmp(digest,expected,20) || seconds<=0) return 5;
  printf("%u,%lu,%lu,%.9f,%.3f,%u\n",mode,(unsigned long)size,iterations,
      seconds,((double)size*iterations)/(seconds*1048576.0),(unsigned)checksum);
  free(input); return 0;
}
