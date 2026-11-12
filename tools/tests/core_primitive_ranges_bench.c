/* Same test ABI and actual methods for deque parent and fixed-storage head.
 * Clocks and CSV writes are exclusively test code. MSVC C89. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#else
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "primitive_counts_protocol.h"
#ifndef XE_SHADER_COMPILE_BENCH
#define XE_SHADER_COMPILE_BENCH
#endif
#include "shader_compile_bench.h"
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"primitive bench line %d\n",__LINE__);exit(1); } } while(0)
static unsigned pinned_group,pinned_cpu;
static int negative_control;
static unsigned setting(const char *name,unsigned fallback,unsigned limit)
{
   const char *text=getenv(name);
   char *end;
   unsigned long value;
   if(!text)return fallback;
   CHECK(*text);value=strtoul(text,&end,10);CHECK(!*end && value<limit);
   return (unsigned)value;
}
static void pin(void)
{
#ifdef _WIN32
   GROUP_AFFINITY affinity,actual;
   pinned_group=setting("PRIMITIVE_BENCH_CPU_GROUP",0,65536);
   pinned_cpu=setting("PRIMITIVE_BENCH_CPU_INDEX",2,(unsigned)(sizeof(DWORD_PTR)*8));
   memset(&affinity,0,sizeof(affinity));affinity.Group=(WORD)pinned_group;
   affinity.Mask=(DWORD_PTR)1<<pinned_cpu;
   CHECK(SetThreadGroupAffinity(GetCurrentThread(),&affinity,NULL));
   CHECK(GetThreadGroupAffinity(GetCurrentThread(),&actual));
   CHECK(actual.Group==affinity.Group && actual.Mask==affinity.Mask);
   printf("PRIMITIVE_AFFINITY group%u cpu%u\n",pinned_group,pinned_cpu);
#else
   /* Current qualification controller is MSVC/Windows only. */
   (void)setting;(void)pinned_group;(void)pinned_cpu;
   puts("PRIMITIVE_AFFINITY unqualified-unpinned");
#endif
}
static void check_cpu(void)
{
#ifdef _WIN32
   PROCESSOR_NUMBER actual;
   GetCurrentProcessorNumberEx(&actual);
   CHECK(actual.Group==pinned_group && actual.Number==pinned_cpu);
#endif
}
static uint16_t source16[65535];
static uint32_t source32[65535],rows[21845*3+8];
static volatile uint32_t checksum;
static unsigned expected_rows,expected_total;
static unsigned host_count(unsigned topology,unsigned count)
{
   if(topology==1)return count>=2?count+1:0;
   if(topology==2)return count/4*6;
   return count>=3?(count-2)*3:0;
}
static void verify(unsigned topology,unsigned width,unsigned count,uint32_t reset,uint32_t mask)
{
   unsigned at=0,begin,n,host,used=0,i;
   for(;;) {
      begin=at;
      while(at<count && (width==2?(uint32_t)source16[at]:(source32[at]&mask))!=reset)++at;
      n=at-begin;host=host_count(topology,n);
      if(host) {
         CHECK(used<21845 && rows[used*3]==begin && rows[used*3+1]==n && rows[used*3+2]==host);
         ++used;
      }
      if(at>=count)break;
      ++at;
   }
   CHECK(used==expected_rows);
   for(i=used*3;i<21845*3+8;++i)CHECK(rows[i]==UINT32_C(0x6d6d6d6d));
}
static void measure(void *context,unsigned which)
{
   unsigned topology=which/8,width=((which/4)&1)?4:2;
   unsigned count=(which&2)?65535:510,stride=(which&1)?3:8;
   unsigned repeats=(which&2)?128:20000,i,used,begin,at,host,burn;
   uint32_t reset=width==2?UINT16_MAX:UINT32_C(0xabcdef);
   uint32_t mask=width==2?UINT16_MAX:UINT32_C(0xffffff),result;
   const void *source=width==2?(const void*)source16:(const void*)source32;
   for(i=0;i<count;++i) {
      source16[i]=i%stride==stride-1?UINT16_MAX:(uint16_t)(i%31+1);
      source32[i]=i%stride==stride-1?UINT32_C(0xf3abcdef):UINT32_C(0xc7000000)+(i%31+1);
   }
   expected_rows=0;expected_total=0;at=0;
   for(;;) {
      begin=at;
      while(at<count && (width==2?(uint32_t)source16[at]:(source32[at]&mask))!=reset)++at;
      host=host_count(topology,at-begin);
      if(host) { ++expected_rows;expected_total+=host; }
      if(at>=count)break;
      ++at;
   }
   memset(rows,0x6d,sizeof(rows));
   CHECK(xe_primitive_counts_run(context,topology,width,source,count,reset,mask,rows,21845,&used)==expected_total);
   CHECK(used==expected_rows);verify(topology,width,count,reset,mask);
   for(i=0;i<16;++i)CHECK(xe_primitive_counts_run(context,topology,width,source,count,reset,mask,NULL,0,&used)==expected_total && used==expected_rows);
   XE_COMPILE_BEGIN();
   for(i=0;i<repeats;++i) {
      result=xe_primitive_counts_run(context,topology,width,source,count,reset,mask,NULL,0,&used);
      CHECK(result==expected_total && used==expected_rows);
      checksum+=result^used;
      /* Test-only real-work regression control; never built into the core. */
      if(negative_control && which==0)for(burn=0;burn<128;++burn)
         CHECK(xe_primitive_counts_run(context,topology,width,source,count,reset,mask,NULL,0,&used)==expected_total && used==expected_rows);
   }
   XE_COMPILE_END("primitive_ranges.warm",which);
   check_cpu();
   memset(rows,0x6d,sizeof(rows));
   CHECK(xe_primitive_counts_run(context,topology,width,source,count,reset,mask,rows,21845,&used)==expected_total && used==expected_rows);
   verify(topology,width,count,reset,mask);
   printf("PRIMITIVE_CASE %u width%u input%u rows%u host%u repeats%u\n",which,width,count,expected_rows,expected_total,repeats);
}
int main(void)
{
   void *context;
   unsigned i;
   int reverse=getenv("PRIMITIVE_BENCH_REVERSE")!=NULL;
   pin();check_cpu();context=xe_primitive_counts_open();CHECK(context);
   negative_control=getenv("PRIMITIVE_BENCH_NEGATIVE")!=NULL;
   printf("PRIMITIVE_CONTROL slow%d\n",negative_control);
   printf("PRIMITIVE_OWNER %u cases32 reverse%d\n",xe_primitive_counts_kind(),reverse);
   for(i=0;i<32;++i)measure(context,reverse?31-i:i);
   xe_primitive_counts_close(context);
   printf("PASS: persistent actual range collection, independent before/after rows, checksum%u\n",checksum);
   return 0;
}
