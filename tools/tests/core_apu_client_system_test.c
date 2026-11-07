/* Actual native client calls and guest heap lifetime. MSVC C89. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "libretro/core_apu_clients.h"
#include <features/features_cpu.h>
void *xe_apu_system_new(void);
void xe_apu_system_free(void *);
int xe_apu_system_register(void *,uint32_t,unsigned *);
void xe_apu_system_unregister(void *,unsigned);
void xe_apu_system_self(void *,unsigned);
void xe_apu_system_collect(void *);
uint64_t xe_apu_system_token(void *,unsigned);
int xe_apu_system_begin(void *,unsigned,uint64_t,uint32_t *,uint32_t *,uint64_t *);
void xe_apu_system_end(void *,unsigned,uint64_t);
unsigned xe_apu_system_credits(void *,unsigned,uint64_t);
int xe_apu_system_stale(void *,unsigned,uint64_t);
void xe_apu_system_submit(void *,unsigned,float *);
int xe_apu_system_stats(void *,unsigned,uint32_t *);
void xe_apu_system_observed(void *,unsigned *);
void xe_apu_system_mode(void *,unsigned);
uint32_t xe_apu_system_pages(void *);
int xe_apu_system_unallocated(void *,uint32_t);
const void *xe_apu_system_argument(void *,uint32_t);
void *xe_apu_legacy_credit_new(void);
void xe_apu_legacy_credit_free(void *);
int xe_apu_legacy_credit_step(void *);
#define CHECK(x) do {if(!(x)){fprintf(stderr,"apu system line %u\n",(unsigned)__LINE__);exit(1);}} while(0)
static float samples[1536];
static int compare_time(const void *a,const void *b) {
  double x=*(const double *)a,y=*(const double *)b;return (x>y)-(x<y);
}
static void benchmark(void) {
  xe_apu_clients *clients=xe_apu_clients_new(1);xe_apu_credit_sink sink;
  unsigned index,run,lane,i,n;uint64_t token,lease;int64_t start;
  double old_times[7],new_times[7],elapsed;void *legacy=xe_apu_legacy_credit_new();
  CHECK(clients && legacy);CHECK(xe_apu_clients_reserve(clients,&index,&token));
  CHECK(xe_apu_clients_publish(clients,index,token,1));
  sink.owner=clients;sink.index=index;sink.token=token;
  for(run=0;run<8;++run) for(lane=0;lane<2;++lane) {
    n=run?100000:2000;start=cpu_features_get_time_usec();
    if((lane+(run&1))&1) {
      for(i=0;i<n;++i) CHECK(xe_apu_legacy_credit_step(legacy));
      elapsed=(double)(cpu_features_get_time_usec()-start)*1000.0/n;
      if(run) old_times[run-1]=elapsed;
    } else {
      for(i=0;i<n;++i) {
        CHECK(xe_apu_clients_acquire(clients,index,&lease));
        CHECK(xe_apu_clients_return_credit(&sink));CHECK(xe_apu_clients_release(clients,index,lease)==0);
      }
      elapsed=(double)(cpu_features_get_time_usec()-start)*1000.0/n;
      if(run) new_times[run-1]=elapsed;
    }
  }
  qsort(old_times,7,sizeof(double),compare_time);qsort(new_times,7,sizeof(double),compare_time);
  printf("CREDIT BENCH median ns/step: legacy wait/release %.2f; packed lease/return/release %.2f; ratio %.2fx\n",old_times[3],new_times[3],old_times[3]/new_times[3]);
  CHECK(xe_apu_clients_retire(clients,index,&token)==XE_APU_RETIRE_OWN);
  CHECK(xe_apu_clients_finish(clients,index,token));xe_apu_clients_free(clients);xe_apu_legacy_credit_free(legacy);
}
int main(void) {
  void *f;unsigned i,index,counts[3],mode;uint32_t pages,callback,arg,stats[3];
  uint64_t token,lease,next;const unsigned char *bytes;
  f=xe_apu_system_new();CHECK(f);pages=xe_apu_system_pages(f);
  for(mode=1;mode<=3;++mode) for(i=0;i<8;++i) {
    xe_apu_system_mode(f,mode);index=99;
    CHECK(!xe_apu_system_register(f,0x11223344,&index) && index==99);
    CHECK(xe_apu_system_pages(f)==pages);
  }
  xe_apu_system_mode(f,0);
  for(i=0;i<8;++i) CHECK(xe_apu_system_register(f,0x11223344,&index) && index==i);
  index=99;CHECK(!xe_apu_system_register(f,0,&index) && index==99);
  CHECK(xe_apu_system_pages(f)==pages-8);
  token=xe_apu_system_token(f,0);CHECK(xe_apu_system_credits(f,0,token)==8);
  CHECK(xe_apu_system_begin(f,0,token,&callback,&arg,&lease));CHECK(callback==0x80000010);
  CHECK(lease==token && xe_apu_system_credits(f,0,token)==7);
  bytes=(const unsigned char *)xe_apu_system_argument(f,arg);
  CHECK(bytes[0]==0x11 && bytes[1]==0x22 && bytes[2]==0x33 && bytes[3]==0x44);
  xe_apu_system_submit(f,0,samples);CHECK(xe_apu_system_credits(f,0,token)==8);
  CHECK(xe_apu_system_stats(f,0,stats) && stats[0]==1 && stats[1]==1 && !stats[2]);
  xe_apu_system_self(f,0);CHECK(!xe_apu_system_stats(f,0,stats));
  CHECK(!xe_apu_system_unallocated(f,arg));
  CHECK(!xe_apu_system_register(f,0,&index)); /* Retired slot cannot be reused. */
  xe_apu_system_submit(f,0,samples);xe_apu_system_observed(f,counts);
  CHECK(counts[0]==8 && !counts[1] && counts[2]==1);
  xe_apu_system_end(f,0,lease);CHECK(!xe_apu_system_unallocated(f,arg));
  xe_apu_system_collect(f);CHECK(xe_apu_system_unallocated(f,arg));
  CHECK(xe_apu_system_register(f,0,&index) && !index);
  next=xe_apu_system_token(f,0);CHECK(next!=token);
  CHECK(!xe_apu_system_begin(f,0,token,&callback,&arg,&lease));
  CHECK(!xe_apu_system_stale(f,0,token));xe_apu_system_end(f,0,token);
  CHECK(xe_apu_system_credits(f,0,next)==8);
  xe_apu_system_observed(f,counts);CHECK(counts[0]==9 && counts[1]==1);
  for(i=0;i<8;++i) xe_apu_system_unregister(f,i);
  CHECK(xe_apu_system_pages(f)==pages);xe_apu_system_observed(f,counts);
  CHECK(counts[0]==counts[1]);
  CHECK(xe_apu_system_register(f,0,&index) && !index);token=xe_apu_system_token(f,0);
  CHECK(xe_apu_system_begin(f,0,token,&callback,&arg,&lease));
  xe_apu_system_self(f,0);xe_apu_system_end(f,0,lease);
  CHECK(xe_apu_system_register(f,0,&index) && !index);
  CHECK(xe_apu_system_pages(f)==pages-1);xe_apu_system_unregister(f,0);
  CHECK(xe_apu_system_register(f,0,&index) && !index);token=xe_apu_system_token(f,0);
  for(i=0;i<8;++i) {CHECK(xe_apu_system_begin(f,0,token,&callback,&arg,&lease));xe_apu_system_end(f,0,lease);}
  CHECK(!xe_apu_system_begin(f,0,token,&callback,&arg,&lease));
  xe_apu_system_submit(f,0,NULL);CHECK(xe_apu_system_credits(f,0,token)==0);
  xe_apu_system_submit(f,0,samples);CHECK(xe_apu_system_credits(f,0,token)==1);
  CHECK(xe_apu_system_begin(f,0,token,&callback,&arg,&lease));xe_apu_system_end(f,0,lease);
  xe_apu_system_unregister(f,0);xe_apu_system_unregister(f,99999);
  xe_apu_system_submit(f,99999,samples);CHECK(!xe_apu_system_stats(f,99999,stats));
  CHECK(xe_apu_system_pages(f)==pages);xe_apu_system_observed(f,counts);CHECK(counts[0]==counts[1]);
  xe_apu_system_free(f);
  puts("PASS: native APU factory rollback, callback credits, self-unregister lifetime, stale selection/release, argument reclamation and bounds");
  if(getenv("APU_CLIENT_BENCH")) benchmark();
  return 0;
}
