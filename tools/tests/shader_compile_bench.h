/* Optional test-only timing. No core/runtime dependency or release overhead. */
#ifndef XE_TEST_SHADER_COMPILE_BENCH_H
#define XE_TEST_SHADER_COMPILE_BENCH_H
#ifdef XE_SHADER_COMPILE_BENCH
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
static double xe_compile_now(void)
{
   LARGE_INTEGER tick,frequency;
   if(!QueryPerformanceFrequency(&frequency) || !QueryPerformanceCounter(&tick))exit(2);
   return (double)tick.QuadPart/(double)frequency.QuadPart;
}
static double xe_compile_cpu_now(void)
{
   FILETIME created,exited,kernel,user;
   ULARGE_INTEGER ticks;
   double seconds;
   if(!GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user))exit(2);
   ticks.LowPart=kernel.dwLowDateTime;ticks.HighPart=kernel.dwHighDateTime;
   seconds=(double)ticks.QuadPart*1e-7;
   ticks.LowPart=user.dwLowDateTime;ticks.HighPart=user.dwHighDateTime;
   return seconds+(double)ticks.QuadPart*1e-7;
}
#else
#include <time.h>
static double xe_compile_now(void)
{
   struct timespec tick;
   if(clock_gettime(CLOCK_MONOTONIC,&tick))exit(2);
   return (double)tick.tv_sec+(double)tick.tv_nsec*1e-9;
}
static double xe_compile_cpu_now(void)
{
   struct timespec tick;
   if(clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&tick))exit(2);
   return (double)tick.tv_sec+(double)tick.tv_nsec*1e-9;
}
#endif
static double xe_compile_started,xe_compile_cpu_started;
static const char *xe_compile_cpu_path;
static void xe_compile_begin(void)
{
   xe_compile_cpu_path=getenv("XE_SHADER_BENCH_CPU_CSV");
   if(xe_compile_cpu_path && *xe_compile_cpu_path)xe_compile_cpu_started=xe_compile_cpu_now();
   xe_compile_started=xe_compile_now();
}
static unsigned xe_compile_case;
static void xe_compile_record(const char *stage,unsigned which)
{
   double elapsed=xe_compile_now()-xe_compile_started;
   double cpu_elapsed=0;
   const char *path=getenv("XE_SHADER_BENCH_CSV");
   FILE *file;
   (void)xe_compile_case;
   if(xe_compile_cpu_path && *xe_compile_cpu_path)cpu_elapsed=xe_compile_cpu_now()-xe_compile_cpu_started;
   if(!path || !*path || elapsed<0)exit(2);
   file=fopen(path,"ab");if(!file)exit(2);
   if(fprintf(file,"%s,%u,%.9f\n",stage,which,elapsed)<0 || fclose(file))exit(2);
   if(xe_compile_cpu_path && *xe_compile_cpu_path) {
      if(cpu_elapsed<0)exit(2);
      file=fopen(xe_compile_cpu_path,"ab");if(!file)exit(2);
      if(fprintf(file,"%s,%u,%.9f\n",stage,which,cpu_elapsed)<0 || fclose(file))exit(2);
   }
}
#define XE_COMPILE_BEGIN() xe_compile_begin()
#define XE_COMPILE_END(stage,which) xe_compile_record(stage,which)
#define XE_COMPILE_CASE(which) (xe_compile_case=(which))
#else
#define XE_COMPILE_BEGIN() ((void)0)
#define XE_COMPILE_END(stage,which) ((void)0)
#define XE_COMPILE_CASE(which) ((void)0)
#endif
#endif
