/* Test-only clock/fault/scheduling controls. No driver or core is involved. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L
#define XE_SHADER_COMPILE_BENCH
#include "shader_compile_resources.h"
#include "shader_compile_bench.h"
#include <sys/mman.h>
#include <time.h>
#include <string.h>

int main(void)
{
   long page=sysconf(_SC_PAGESIZE);
   size_t bytes,i;
   volatile unsigned char *memory;
   struct timespec delay={0,20000000};
   const char *negative=getenv("XE_RESOURCE_TEST_SLOW");
   double end;
   if(page<=0 || (unsigned long)page>1048576UL)return 2;
   bytes=(size_t)page*256;
   memory=(volatile unsigned char *)mmap(NULL,bytes,PROT_READ|PROT_WRITE,
      MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
   if(memory==MAP_FAILED || madvise((void *)memory,bytes,MADV_NOHUGEPAGE))return 2;
   XE_COMPILE_BEGIN();
   if(negative && !strcmp(negative,"1")) {
      end=xe_compile_cpu_now()+.05;
      while(xe_compile_cpu_now()<end) {}
   }
   XE_COMPILE_END("vulkan.resource_control",0);
   XE_COMPILE_BEGIN();
   for(i=0;i<256;++i)memory[i*(size_t)page]=(unsigned char)i;
   XE_COMPILE_END("vulkan.resource_control",1);
   XE_COMPILE_BEGIN();
   if(nanosleep(&delay,NULL))return 2;
   XE_COMPILE_END("vulkan.resource_control",2);
   if(munmap((void *)memory,bytes))return 2;
   return 0;
}
