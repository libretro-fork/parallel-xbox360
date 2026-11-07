/* Synthetic request metadata only; includes identical input copies in both. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 199309L
#endif
#include <stdio.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include "libretro/core_pointer_unique.h"
struct item { unsigned char bytes[64]; };
XE_POINTER_UNIQUE_DEFINE(unique_items, struct item)
#if defined(_MSC_VER)
#define NOINLINE __declspec(noinline)
#else
#define NOINLINE __attribute__((noinline))
#endif
static struct item pool[64];
static struct item *input[64], *work[64];
static volatile unsigned checksum;
static NOINLINE unsigned bounded(unsigned n) {return unique_items(work,n);}
static NOINLINE unsigned linear(unsigned n) {
   unsigned i,j,k=0;
   for(i=0;i<n;++i) {
      for(j=0;j<k && work[j]!=work[i];++j) {}
      if(j==k) work[k++]=work[i];
   }
   return k;
}
static double now(void) {
#ifdef _WIN32
   LARGE_INTEGER value,frequency;
   QueryPerformanceCounter(&value);QueryPerformanceFrequency(&frequency);
   return (double)value.QuadPart/frequency.QuadPart;
#else
   struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
   return (double)t.tv_sec+(double)t.tv_nsec*1e-9;
#endif
}
static double measure(unsigned n,unsigned runs,int which) {
   unsigned i,sum=0;double begin=now();
   for(i=0;i<runs;++i) {memcpy(work,input,n*sizeof(*input));sum+=which?bounded(n):linear(n);}
   checksum+=sum;
   return ((now()-begin)*1e9)/runs;
}
int main(void) {
   unsigned n,i,distinct,pass;double a,b,aa,bb;
   puts("count distinct linear_ns bounded_ns ratio (mean3; input copy included)");
   for(n=2;n<=64;n*=2) for(distinct=1;distinct<=n;distinct*=2) {
      for(i=0;i<n;++i) input[i]=&pool[i%distinct];
      a=b=0;
      /* Average3 alternated order; noise is reported, no gameplay claim. */
      for(pass=0;pass<3;++pass) {
         if(pass&1) {bb=measure(n,500000,1);aa=measure(n,500000,0);}
         else {aa=measure(n,500000,0);bb=measure(n,500000,1);}
         a+=aa;b+=bb;
      }
      printf("%u %u %.2f %.2f %.3f\n",n,distinct,a/3,b/3,b/a);
   }
   return checksum==0;
}
