/* Test-only CPU/wall separation under a controlled blocking read. C89. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#define XE_SHADER_COMPILE_BENCH
#include "shader_compile_bench.h"
int main(void)
{
   volatile unsigned state=17;
   unsigned i;
   double cpu_start;
   XE_COMPILE_BEGIN();
   if(puts("ready")<0 || fflush(stdout) || getchar()!='\n')return 1;
   XE_COMPILE_END("clock.blocked",0);
   cpu_start=xe_compile_cpu_now();
   XE_COMPILE_BEGIN();
   do {
      for(i=0;i<4096;++i)state=state*1664525u+1013904223u;
      if(xe_compile_now()-xe_compile_started>5.0)return 1;
   } while(xe_compile_cpu_now()-cpu_start<0.06);
   XE_COMPILE_END("clock.busy",0);
   return 0;
}
