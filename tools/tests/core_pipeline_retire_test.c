#include "libretro/core_pipeline_retire.h"
#include <rthreads/rthreads.h>
#include <features/features_cpu.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) {fprintf(stderr,"retire line %u\n",(unsigned)__LINE__);exit(1);} } while (0)
#define COUNT 512
static xe_pipeline_retire_slot *slots[COUNT];
static unsigned seen[COUNT], released;
static void destroy(void *opaque, uint64_t handle)
{
   unsigned index = (unsigned)(handle - UINT64_C(0xFEDCBA9800000000));
   CHECK(opaque == seen && index < COUNT && !seen[index]);
   seen[index] = 1;
   ++released;
}
static void publish(void *opaque)
{
   size_t i, start = (size_t)opaque;
   for (i = start; i < COUNT; i += 8)
      xe_pipeline_retire_publish(slots[i], UINT64_C(0xFEDCBA9800000000) + i, i);
}
int main(void)
{
   xe_pipeline_retire_pool *pool;
   xe_pipeline_retire_slot *a, *b, *c;
   sthread_t *workers[8];
   size_t i, round;
   int64_t start;
   CHECK(!xe_pipeline_retire_new(0));
   CHECK(!xe_pipeline_retire_new((size_t)-1));
   CHECK(!xe_pipeline_retire_reserve(NULL));
   CHECK(xe_pipeline_retire_free(NULL, NULL, NULL));
   pool = xe_pipeline_retire_new(3); CHECK(pool);
   a=xe_pipeline_retire_reserve(pool); b=xe_pipeline_retire_reserve(pool);
   c=xe_pipeline_retire_reserve(pool);
   CHECK(a && b && c && !xe_pipeline_retire_available(pool));
   CHECK(!xe_pipeline_retire_reserve(pool));
   xe_pipeline_retire_cancel(c);
   CHECK(xe_pipeline_retire_available(pool)==1);
   c=xe_pipeline_retire_reserve(pool);
   CHECK(!xe_pipeline_retire_transfer(&a, 0, 100) && a);
   CHECK(xe_pipeline_retire_transfer(&a, UINT64_C(0xFEDCBA9800000000), 100));
   CHECK(!a && !xe_pipeline_retire_transfer(&a, UINT64_C(0xFEDCBA9800000000), 100));
   xe_pipeline_retire_publish(b, UINT64_C(0xFEDCBA9800000001), 200);
   xe_pipeline_retire_publish(c, 0, UINT64_MAX);
   CHECK(!xe_pipeline_retire_collect(pool, 99, 0, destroy, seen));
   CHECK(xe_pipeline_retire_available(pool)==1);
   CHECK(!xe_pipeline_retire_collect(pool, 200, 1, NULL, seen));
   CHECK(xe_pipeline_retire_collect(pool, 100, 1, destroy, seen)==1);
   CHECK(seen[0] && !seen[1]);
   CHECK(!xe_pipeline_retire_free(pool, NULL, NULL));
   CHECK(xe_pipeline_retire_free(pool, destroy, seen)); CHECK(seen[1]);
   pool=xe_pipeline_retire_new(COUNT); CHECK(pool);
   for (round=0; round<20; ++round) {
      memset(seen,0,sizeof(seen)); released=0;
      for(i=0;i<COUNT;++i) { slots[i]=xe_pipeline_retire_reserve(pool); CHECK(slots[i]); }
      for(i=0;i<8;++i) { workers[i]=sthread_create(publish,(void *)i); CHECK(workers[i]); }
      start=cpu_features_get_time_usec();
      while(released<COUNT) {
         xe_pipeline_retire_collect(pool,UINT64_MAX,11,destroy,seen);
         CHECK(cpu_features_get_time_usec()-start<10000000);
      }
      for(i=0;i<8;++i) sthread_join(workers[i]);
      CHECK(xe_pipeline_retire_available(pool)==COUNT);
      /* Reuse while the array owns all nodes; shutdown leaves unpublished
       * handles with their external entries, without inventing destructions. */
      CHECK(released==COUNT);
   }
   CHECK(xe_pipeline_retire_reserve(pool));
   CHECK(xe_pipeline_retire_free(pool,destroy,seen));
   puts("PASS retirement reservation, completion gates, budget, concurrent publish/collect, reuse and shutdown");
   return 0;
}
