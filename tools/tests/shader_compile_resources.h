/* Linux test-only observations outside the measured wall/CPU interval.
 * These explain neither causality nor absence of scheduling inside the interval. */
#ifndef XE_TEST_SHADER_COMPILE_RESOURCES_H
#define XE_TEST_SHADER_COMPILE_RESOURCES_H
#ifndef __linux__
#error Linux resource observations require a Linux test runner
#endif
#include <stdio.h>
#include <stdlib.h>
#include <sched.h>
#include <sys/resource.h>
#include <unistd.h>

static struct rusage xe_compile_resource_start;
static int xe_compile_resource_cpu;
static const char *xe_compile_resource_path;

static void xe_compile_resource_begin(void)
{
   xe_compile_resource_path=getenv("XE_SHADER_BENCH_RESOURCE_CSV");
   if(!xe_compile_resource_path || !*xe_compile_resource_path)return;
   if(getrusage(RUSAGE_SELF,&xe_compile_resource_start))exit(2);
   xe_compile_resource_cpu=sched_getcpu();
   if(xe_compile_resource_cpu<0)exit(2);
}

static void xe_compile_resource_end(const char *stage,unsigned which)
{
   struct rusage end;
   int cpu;
   FILE *file;
   if(!xe_compile_resource_path || !*xe_compile_resource_path)return;
   cpu=sched_getcpu();
   if(cpu<0 || getrusage(RUSAGE_SELF,&end))exit(2);
   if(end.ru_minflt<xe_compile_resource_start.ru_minflt ||
      end.ru_majflt<xe_compile_resource_start.ru_majflt ||
      end.ru_nvcsw<xe_compile_resource_start.ru_nvcsw ||
      end.ru_nivcsw<xe_compile_resource_start.ru_nivcsw ||
      end.ru_maxrss<xe_compile_resource_start.ru_maxrss)exit(2);
   file=fopen(xe_compile_resource_path,"ab");if(!file)exit(2);
   if(fprintf(file,"%s,%u,%ld,%d,%d,%ld,%ld,%ld,%ld,%ld,%ld\n",stage,which,(long)getpid(),
      xe_compile_resource_cpu,cpu,end.ru_minflt-xe_compile_resource_start.ru_minflt,
      end.ru_majflt-xe_compile_resource_start.ru_majflt,
      end.ru_nvcsw-xe_compile_resource_start.ru_nvcsw,
      end.ru_nivcsw-xe_compile_resource_start.ru_nivcsw,
      xe_compile_resource_start.ru_maxrss,end.ru_maxrss)<0 || fclose(file))exit(2);
}

#define XE_COMPILE_RESOURCE_BEGIN() xe_compile_resource_begin()
#define XE_COMPILE_RESOURCE_END(stage,which) xe_compile_resource_end(stage,which)
#endif
