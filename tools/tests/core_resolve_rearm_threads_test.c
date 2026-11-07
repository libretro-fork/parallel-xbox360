/* Real rthreads: fault marks the new pending map while GPU drains/clears the
 * detached map. Phase gates are test-only; production uses its existing lock. */
#include <stdio.h>
#include <string.h>
#include <rthreads/rthreads.h>
#include <retro_atomic.h>
#include "core_resolve_rearm.h"
#define EPOCHS 2000
static struct shared_state {
  xe_resolve_rearm queue;
  slock_t *lock;
  retro_atomic_int_t phase, error;
  unsigned char expected[2][XE_MEMEXPORT_PAGE_COUNT];
} shared;
static void fault(void *opaque) {
  struct shared_state *s=(struct shared_state *)opaque;
  uint32_t epoch,j;
  for(epoch=0;epoch<EPOCHS;++epoch) {
    uint32_t destination=(epoch+1)&1;
    while(retro_atomic_load_acquire_int(&s->phase)!=(int)(epoch*2+1)) {}
    for(j=0;j<128;++j) {
      uint32_t base=((epoch*263+j*17)%XE_MEMEXPORT_PAGE_COUNT)*4096+(j&1?17:0);
      uint32_t size=j%7?29:8192,p,end=(base+size-1)>>12;
      if(end>=XE_MEMEXPORT_PAGE_COUNT)end=XE_MEMEXPORT_PAGE_COUNT-1;
      slock_lock(s->lock);
      if(!xe_resolve_rearm_mark(&s->queue,base,size))retro_atomic_store_relaxed_int(&s->error,1);
      for(p=base>>12;p<=end;++p)s->expected[destination][p]=1;
      slock_unlock(s->lock);
    }
    retro_atomic_store_release_int(&s->phase,(int)(epoch*2+2));
  }
}
static void consume(struct shared_state *s,xe_memexport_cursor *cursor,uint32_t index) {
  xe_memexport_run run;
  uint32_t p,last=0;
  while(xe_memexport_next(cursor,&run)) {
    if(!run.size||run.base<last||(run.base&4095)||(run.size&4095))retro_atomic_store_relaxed_int(&s->error,1);
    for(p=last>>12;p<run.base>>12;++p)if(s->expected[index][p])retro_atomic_store_relaxed_int(&s->error,1);
    for(p=run.base>>12;p<(run.base+run.size)>>12;++p){
      if(!s->expected[index][p])retro_atomic_store_relaxed_int(&s->error,1);
      s->expected[index][p]=0;
    }
    last=run.base+run.size;
  }
  for(p=0;p<XE_MEMEXPORT_PAGE_COUNT;++p)if(s->expected[index][p])retro_atomic_store_relaxed_int(&s->error,1);
}
int main(void) {
  struct shared_state *s=&shared;
  sthread_t *thread;
  uint32_t epoch;
  xe_resolve_rearm_reset(&s->queue);s->lock=slock_new();if(!s->lock)return 1;
  xe_resolve_rearm_mark(&s->queue,4095,2);s->expected[0][0]=s->expected[0][1]=1;
  thread=sthread_create(fault,s);if(!thread){slock_free(s->lock);return 1;}
  for(epoch=0;epoch<EPOCHS;++epoch) {
    xe_memexport_cursor cursor;
    slock_lock(s->lock);
    if(!xe_resolve_rearm_begin(&s->queue,&cursor)){slock_unlock(s->lock);return 1;}
    slock_unlock(s->lock);
    retro_atomic_store_release_int(&s->phase,(int)(epoch*2+1));
    consume(s,&cursor,epoch&1);
    xe_resolve_rearm_finish(&s->queue);
    while(retro_atomic_load_acquire_int(&s->phase)!=(int)(epoch*2+2)) {}
  }
  sthread_join(thread);
  {xe_memexport_cursor cursor;
   if(!xe_resolve_rearm_begin(&s->queue,&cursor))return 1;
   consume(s,&cursor,EPOCHS&1);xe_resolve_rearm_finish(&s->queue);}
  slock_free(s->lock);
  if(retro_atomic_load_relaxed_int(&s->error))return 1;
  puts("Rearm rthreads: 2000 detached leases and 256000 concurrent next-batch marks preserve exact page unions; borrowed map stays stable through finish");return 0;
}
