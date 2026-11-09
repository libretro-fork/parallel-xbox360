/* Redistributable procedural guest corpus; fresh analysis/translation. C89. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define XE_SHADER_COMPILE_BENCH
#include "shader_compile_bench.h"
#include "shader_call_fixture.h"
void *xe_compile_open(unsigned);
void xe_compile_close(void*);
int xe_compile_step(void*,const uint32_t*,size_t,unsigned,const void**,size_t*);
static uint32_t code[12288];
static size_t stress(unsigned pixel,unsigned additions,unsigned dot,unsigned scenario)
{
   unsigned count=additions+1,chunks=(count+5)/6,payload=(chunks+1)/2,i,n;
   memset(code,0,sizeof(code));
   for(i=0;i<chunks;++i) {
      n=count-i*6;if(n>6)n=6;
      cf(code,i,(payload+i*6)|(n<<12),i+1==chunks?0x2000:0x1000);
   }
   for(i=0;i<additions;++i) {
      code[(payload+i)*3]=0xc80f0000;
      code[(payload+i)*3+2]=dot?
         UINT32_C(0x80000000)|((1u+scenario)<<8)|(64u+scenario)|((dot==3?11u:dot==2?8u:15u+scenario%3)<<24):
         UINT32_C(0xc0000100);
   }
   code[(payload+additions)*3]=(pixel?0:62)|(1u<<15)|(15u<<16)|(50u<<26);
   code[(payload+additions)*3+2]=0xc2000000;
   return (payload+count)*3;
}
int main(int argc,char **argv)
{
   static const unsigned additions[3]={32,96,256};
   unsigned profile,pixel,scenario,repeat,id=0;
   size_t count,bytes=0;const void *data=NULL;void *context;
   const char *no_dump=getenv("XE_SHADER_BENCH_NO_DUMP");
   const char *rtz=getenv("XE_SHADER_BENCH_RTZ_PROFILE");
   unsigned hardware_rtz=rtz && !strcmp(rtz,"1");
   const char *dot=getenv("XE_SHADER_BENCH_DOT_PROFILE");
   unsigned dot_profile=dot && !strcmp(dot,"1");
   const char *frac=getenv("XE_SHADER_BENCH_FRAC_PROFILE");
   unsigned frac_profile=frac && !strcmp(frac,"1");
   const char *mad=getenv("XE_SHADER_BENCH_MAD_PROFILE");
   unsigned mad_profile=mad && !strcmp(mad,"1");
   const char *stage=mad_profile?"spirv.mad_translate_batch4":frac_profile?"spirv.frac_translate_batch4":dot_profile?"spirv.dot_translate_batch4":
       hardware_rtz?"spirv.rtz_translate_batch4":"spirv.translate_batch4";
   char path[512];FILE *file;
   if(argc!=2 || strlen(argv[1])>400)return 2;
   for(profile=0;profile<4;++profile) {
      context=xe_compile_open(profile|((hardware_rtz || dot_profile || frac_profile || mad_profile)?8u:0u));if(!context)return 1;
      for(pixel=0;pixel<2;++pixel)for(scenario=0;scenario<8;++scenario,++id) {
         count=mad_profile?stress(pixel,additions[scenario%3],3,scenario):
               frac_profile?stress(pixel,additions[scenario%3],2,scenario):
               dot_profile?stress(pixel,additions[scenario%3],1,scenario):
               scenario<5?nested_scenario(code,pixel,scenario):
               stress(pixel,additions[scenario-5],0,0);
         XE_COMPILE_BEGIN();
         for(repeat=0;repeat<4;++repeat)
            if(!xe_compile_step(context,code,count,pixel,&data,&bytes))return 1;
         XE_COMPILE_END(stage,id);
         if(!data || bytes<20 || bytes%4)return 1;
         if(no_dump && !strcmp(no_dump,"1"))continue;
         sprintf(path,"%s/shader%03u.spv",argv[1],id);
         file=fopen(path,"wb");if(!file)return 1;
         if(fwrite(data,1,bytes,file)!=bytes || fclose(file))return 1;
      }
      xe_compile_close(context);
   }
   printf("PASS: %u procedural VS/PS cases, nested calls and ALU stress, native/portable swizzle,1x/4x\n",id);
   return id==64?0:1;
}
