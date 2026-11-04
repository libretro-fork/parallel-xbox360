/* Production guarded-fiber benchmark. C89; Windows or Linux host. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#else
#include <time.h>
#include <sys/resource.h>
#endif
extern void *xe_test_fiber_adopt(void);
extern void *xe_test_fiber_create(void (*entry)(void));
extern void xe_test_fiber_switch(void *fiber);
extern void xe_test_fiber_delete(void *fiber);
extern void xe_test_fiber_shutdown(void);
struct context { void *handle; };
struct memory_sample { unsigned long peak_rss,commit; };
static void *parent;
static void entry(void) { for(;;) xe_test_fiber_switch(parent); }
static void adopt(void) { parent=xe_test_fiber_adopt(); if(!parent) exit(3); }
static int create(struct context *c)
{
   c->handle=xe_test_fiber_create(entry);
   return c->handle!=NULL;
}
static void switch_to(struct context *c) { xe_test_fiber_switch(c->handle); }
static void destroy(struct context *c)
{
   xe_test_fiber_delete(c->handle);
   c->handle=NULL;
}
static void finish(void)
{
   xe_test_fiber_delete(parent);
   xe_test_fiber_shutdown();
}
static double now(void) {
#ifdef _WIN32
   LARGE_INTEGER value,freq;
   QueryPerformanceCounter(&value); QueryPerformanceFrequency(&freq);
   return (double)value.QuadPart/(double)freq.QuadPart;
#else
   struct timespec value;
   clock_gettime(CLOCK_MONOTONIC,&value);
   return (double)value.tv_sec+(double)value.tv_nsec*1e-9;
#endif
}
static void memory_sample(struct memory_sample *out) {
#ifdef _WIN32
   PROCESS_MEMORY_COUNTERS_EX memory;
   memset(&memory,0,sizeof(memory)); memory.cb=sizeof(memory);
   if(!GetProcessMemoryInfo(GetCurrentProcess(),(PROCESS_MEMORY_COUNTERS*)&memory,sizeof(memory))) exit(3);
   out->peak_rss=(unsigned long)(memory.PeakWorkingSetSize/1024);
   out->commit=(unsigned long)(memory.PrivateUsage/1024);
#else
   struct rusage memory;
   if(getrusage(RUSAGE_SELF,&memory)) exit(3);
   out->peak_rss=(unsigned long)memory.ru_maxrss; out->commit=0;
#endif
}
int main(int argc,char **argv) {
   const char *mode=argc>1?argv[1]:"switch";
   unsigned long count=argc>2?strtoul(argv[2],NULL,10):2000000;
   unsigned long n=argc>3?strtoul(argv[3],NULL,10):1;
   unsigned long i,j=0,created=0;
   uint32_t random=360;
   int random_order=argc>4 && !strcmp(argv[4],"random");
   struct context *contexts;
   struct memory_sample before,live,after;
   double begin,end,ns;
#ifdef _WIN32
   SetErrorMode(SEM_NOGPFAULTERRORBOX|SEM_FAILCRITICALERRORS);
   if(!SetThreadAffinityMask(GetCurrentThread(),(DWORD_PTR)4)) return 3;
#endif
   if(!count || !n || n>2048 || (n & (n-1)) ||
         (strcmp(mode,"switch") && strcmp(mode,"create") && strcmp(mode,"cold_create"))) return 2;
   contexts=(struct context*)calloc(n+1,sizeof(*contexts)); if(!contexts) return 2;
   adopt(); memory_sample(&before);
   if(!strcmp(mode,"cold_create")) {
      begin=now();
      if(!create(&contexts[0])) goto fail;
      end=now(); created=1;
      ns=(end-begin)*1e9; memory_sample(&live);
   } else {
   for(i=0;i<n;++i) { if(!create(&contexts[i])) goto fail; ++created; }
   if(!strcmp(mode,"create")) {
      for(i=0;i<1024;++i) { if(!create(&contexts[n])) goto fail; destroy(&contexts[n]); }
      memory_sample(&live);
      begin=now();
      for(i=0;i<count;++i) { if(!create(&contexts[n])) goto fail; destroy(&contexts[n]); }
      end=now(); ns=(end-begin)*1e9/(double)count;
   } else {
      for(i=0;i<100000;++i) switch_to(&contexts[i & (n-1)]);
      memory_sample(&live);
      begin=now();
      for(i=0;i<count;++i) {
         if(random_order) { random^=random<<13; random^=random>>17; random^=random<<5; j=random & (n-1); }
         else j=(j+1) & (n-1);
         switch_to(&contexts[j]);
      }
      end=now(); ns=(end-begin)*1e9/(2.0*(double)count);
   }
   }
   for(i=0;i<created;++i) destroy(&contexts[i]);
   finish(); free(contexts); memory_sample(&after);
   printf("%s,%lu,%lu,%.9f,%.3f,%lu,%lu,%lu,%lu,%lu\n",mode,n,count,end-begin,ns,
         before.peak_rss,live.peak_rss,after.peak_rss,before.commit,live.commit);
   return 0;
fail:
   for(i=0;i<created;++i) destroy(&contexts[i]);
   finish(); free(contexts); return 2;
}
