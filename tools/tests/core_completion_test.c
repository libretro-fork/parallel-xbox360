/* Generation ownership and release/acquire visibility. MSVC C89. */
#include "core_completion.h"
#include <rthreads/rthreads.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if(!(x)) {fprintf(stderr,"completion line %u\n",(unsigned)__LINE__);exit(1);} } while(0)
#define ROUNDS 50000
#define WORKERS 4
typedef struct state {
  xe_completion latch;
  retro_atomic_int_t round,done;
  uint64_t stale;
} state;
static void finish_stale(void *opaque) {
  state *s=(state*)opaque;
  unsigned i;
  for(i=1;i<=ROUNDS;++i) {
    while(retro_atomic_load_acquire_int(&s->round)!=(int)i) {}
    CHECK(!xe_completion_finish(&s->latch,s->stale));
    retro_atomic_fetch_add_int(&s->done,1);
    while(retro_atomic_load_acquire_int(&s->round)==(int)i) {}
    retro_atomic_fetch_add_int(&s->done,1);
  }
}
int main(void) {
  state s;
  sthread_t *threads[WORKERS];
  uint64_t ticket,old;
  unsigned i,j;
  xe_completion_init(&s.latch);
  CHECK(!xe_completion_pending(&s.latch,0));CHECK(!xe_completion_finish(&s.latch,0));
  retro_atomic_int_init(&s.round,0);retro_atomic_int_init(&s.done,0);
  for(j=0;j<WORKERS;++j) {threads[j]=sthread_create(finish_stale,&s);CHECK(threads[j]);}
  for(i=1;i<=ROUNDS;++i) {
    old=xe_completion_begin(&s.latch);
    ticket=xe_completion_begin(&s.latch);s.stale=old;
    CHECK(!xe_completion_pending(&s.latch,old));CHECK(xe_completion_pending(&s.latch,ticket));
    retro_atomic_store_release_int(&s.done,0);
    retro_atomic_store_release_int(&s.round,(int)i);
    while(retro_atomic_load_acquire_int(&s.done)!=WORKERS) {}
    CHECK(xe_completion_pending(&s.latch,ticket));
    CHECK(xe_completion_finish(&s.latch,ticket));CHECK(!xe_completion_finish(&s.latch,ticket));
    CHECK(!xe_completion_pending(&s.latch,ticket));
    xe_completion_cancel(&s.latch);CHECK(!xe_completion_finish(&s.latch,ticket));
    retro_atomic_store_release_int(&s.round,0);
    /* Ensure every worker observes the phase boundary before another round. */
    while(retro_atomic_load_acquire_int(&s.done)!=2*WORKERS) {}
  }
  for(j=0;j<WORKERS;++j) sthread_join(threads[j]);
  retro_atomic_store_release_64(&s.latch.state,INT64_MAX-1);
  ticket=xe_completion_begin(&s.latch);CHECK(ticket==((uint64_t)INT64_MAX+2));
  CHECK(xe_completion_finish(&s.latch,ticket));
  retro_atomic_store_release_64(&s.latch.state,(int64_t)(UINT64_MAX-3));
  old=xe_completion_begin(&s.latch);CHECK(old==UINT64_MAX);
  ticket=xe_completion_begin(&s.latch);CHECK(ticket==1);
  CHECK(!xe_completion_finish(&s.latch,old));CHECK(xe_completion_pending(&s.latch,ticket));
  CHECK(xe_completion_finish(&s.latch,ticket));
  puts("PASS: 200000 concurrent stale completions cannot clear current generation");return 0;
}
