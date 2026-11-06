/* C89 release/acquire visibility, 64-bit indices and one-shot capture flag. */
#include <stdio.h>
#include <rthreads/rthreads.h>
#include "core_submission_counter.h"
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"submission line %d: %s\n",__LINE__,#x); return 1; } } while(0)
struct state {
  xe_submission_counter submitted;
  retro_atomic_int_t ack,error;
  uint64_t base;
  uint32_t payload[8];
};
static void publish(void *opaque) {
  struct state *s=(struct state *)opaque;
  unsigned i,j;
  for(i=1;i<=40000;++i) {
    while(retro_atomic_load_acquire_int(&s->ack)!=(int)i-1) {}
    for(j=0;j<8;++j) s->payload[j]=i*37+j;
    xe_submission_publish(&s->submitted,s->base+i);
  }
}
static void consume(void *opaque) {
  struct state *s=(struct state *)opaque;
  unsigned i,j;
  for(i=1;i<=40000;++i) {
    while(xe_submission_acquire(&s->submitted)!=s->base+i) {}
    for(j=0;j<8;++j) if(s->payload[j]!=i*37+j) retro_atomic_store_relaxed_int(&s->error,1);
    retro_atomic_store_release_int(&s->ack,(int)i);
  }
}
int main(void) {
  struct state state;
  sthread_t *writer,*reader;
  retro_atomic_int_t capture;
  unsigned phase;
  for(phase=0;phase<2;++phase) {
    state.base=(uint64_t)1<<(phase ? 63 : 40);
    retro_atomic_64_init(&state.submitted,0);
    retro_atomic_int_init(&state.ack,0); retro_atomic_int_init(&state.error,0);
    writer=sthread_create(publish,&state); CHECK(writer);
    reader=sthread_create(consume,&state); CHECK(reader);
    sthread_join(writer); sthread_join(reader);
    CHECK(!retro_atomic_load_relaxed_int(&state.error));
    CHECK(xe_submission_acquire(&state.submitted)==state.base+40000);
  }
  retro_atomic_int_init(&capture,0);
  retro_atomic_store_relaxed_int(&capture,1);
  CHECK(retro_atomic_exchange_int(&capture,0)==1 && retro_atomic_exchange_int(&capture,0)==0);
  puts("Submission counter: 80,000 release/acquire payload handoffs and 64-bit index preservation pass");
  return 0;
}
