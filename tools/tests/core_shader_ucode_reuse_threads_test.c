/* Contended ownership transfer with real libretro-common threads. C89. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <retro_atomic.h>
#include <rthreads/rthreads.h>
#include "libretro/core_shader_ucode_reuse.h"
static retro_atomic_int_t failed;
static void worker(void *arg)
{
 uintptr_t id=(uintptr_t)arg;unsigned round,j;size_t i,count;
 xe_shader_ucode_reuse owners[2];uint32_t value;
 memset(owners,0,sizeof(owners));
 for(round=0;round<20000;++round){
  count=1024+((round+(unsigned)id)%3)*8;
  for(j=0;j<2;++j){
   if(!xe_shader_ucode_reuse_allocate(&owners[j],count)){
    retro_atomic_store_relaxed_int(&failed,1);goto done;
   }
   value=(uint32_t)(id*100000+round*2+j);
   for(i=0;i<count;++i)owners[j].words[i]=value;
  }
  if(owners[0].words==owners[1].words)retro_atomic_store_relaxed_int(&failed,1);
  for(j=0;j<2;++j){
   value=(uint32_t)(id*100000+round*2+j);
   for(i=0;i<count;++i)if(owners[j].words[i]!=value)retro_atomic_store_relaxed_int(&failed,1);
   xe_shader_ucode_reuse_close(&owners[j]);
  }
 }
done:
 xe_shader_ucode_reuse_close(&owners[0]);xe_shader_ucode_reuse_close(&owners[1]);
}
int main(void)
{
 sthread_t *threads[6];unsigned i,started=0;
 memset(threads,0,sizeof(threads));
 for(i=0;i<6;++i){threads[i]=sthread_create(worker,(void*)(uintptr_t)(i+1));if(!threads[i])break;++started;}
 for(i=0;i<started;++i)sthread_join(threads[i]);
 xe_shader_ucode_reuse_clear();
 if(started!=6 || retro_atomic_load_relaxed_int(&failed)){fputs("reuse concurrency failure\n",stderr);return 1;}
 puts("PASS: six rthreads, 240000 exclusive payload lifetimes, contended exchange/eviction");return 0;
}
