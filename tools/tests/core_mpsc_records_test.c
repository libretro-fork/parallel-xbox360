/* C89: publication gaps, variable spans, FIFO ownership, wrap and stop. */
#include "libretro/core_mpsc_records.h"
#include <retro_atomic.h>
#include <rthreads/rthreads.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"MPSC line %u\n",(unsigned)__LINE__);exit(1);}}while(0)
#define CAPACITY 512
#define UNIT 32
#define RECORDS 12000
struct header {size_t span;unsigned producer,index;};
struct state {xe_mpsc_records *queue;unsigned char *storage;unsigned producer;};
static void put_bytes(unsigned char *p,size_t offset,size_t n,unsigned char value)
{size_t first=CAPACITY*UNIT-offset;if(first>n)first=n;memset(p+offset,value,first);memset(p,value,n-first);}
static void producer(void *opaque)
{
 struct state *s=(struct state*)opaque;size_t ticket,span,offset;unsigned i;struct header h;
 for(i=0;i<RECORDS;++i){
  span=1+(i*7+s->producer*3)%31;
  CHECK(xe_mpsc_records_claim(s->queue,span,&ticket));offset=(ticket&(CAPACITY-1))*UNIT;
  put_bytes(s->storage,offset,span*UNIT,(unsigned char)(i+s->producer));
  h.span=span;h.producer=s->producer;h.index=i;memcpy(s->storage+offset,&h,sizeof(h));
  if(i&1)xe_mpsc_records_publish_notify(s->queue,ticket,span);
  else {xe_mpsc_records_publish(s->queue,ticket,span);xe_mpsc_records_notify(s->queue);}
 }
}
static void run(unsigned producers,size_t seed)
{
 struct state states[8];sthread_t *threads[8];unsigned expected[8]={0};
 xe_mpsc_records *q=xe_mpsc_records_new(CAPACITY);unsigned char *storage;
 size_t count,ticket,offset,total=0,check;unsigned i,batch=0;struct header h;
 CHECK(q && producers<=8);storage=(unsigned char*)calloc(CAPACITY,UNIT);CHECK(storage);
 xe_mpsc_records_seed_empty(q,seed);
 for(i=0;i<producers;++i){states[i].queue=q;states[i].storage=storage;states[i].producer=i;threads[i]=sthread_create(producer,states+i);CHECK(threads[i]);}
 while(total<producers*RECORDS){
  count=xe_mpsc_records_peek(q,&ticket);
  if(!count){xe_mpsc_records_commit(q);CHECK(xe_mpsc_records_wait(q));batch=0;continue;}
  offset=(ticket&(CAPACITY-1))*UNIT;memcpy(&h,storage+offset,sizeof(h));
  CHECK(h.span==count && h.producer<producers && h.index==expected[h.producer]++);
  for(check=sizeof(h);check<count*UNIT;++check)CHECK(storage[(offset+check)%(CAPACITY*UNIT)]==(unsigned char)(h.index+h.producer));
  xe_mpsc_records_advance(q,count);++total;
  if(++batch==16){xe_mpsc_records_commit(q);batch=0;}
 }
 xe_mpsc_records_commit(q);
 for(i=0;i<producers;++i){sthread_join(threads[i]);CHECK(expected[i]==RECORDS);}
 xe_mpsc_records_await(q,seed); /* seed already retired. */
 xe_mpsc_records_free(q);free(storage);
}
struct stopped {xe_mpsc_records *q;int result;int writer;};
static void waiter(void *opaque)
{struct stopped*s=(struct stopped*)opaque;size_t ticket;s->result=s->writer?xe_mpsc_records_claim(s->q,1,&ticket):xe_mpsc_records_wait(s->q);}
int main(void)
{
 xe_mpsc_records *q;size_t a,b,t;struct stopped s;sthread_t *thread;unsigned i;
 CHECK(!xe_mpsc_records_new(0) && !xe_mpsc_records_new(3));
 q=xe_mpsc_records_new(8);CHECK(q);
 CHECK(!xe_mpsc_records_claim(q,0,&a) && !xe_mpsc_records_claim(q,9,&a));
 CHECK(xe_mpsc_records_claim(q,3,&a) && xe_mpsc_records_claim(q,2,&b));
 xe_mpsc_records_publish(q,b,2);CHECK(!xe_mpsc_records_peek(q,&t));
 xe_mpsc_records_publish(q,a,3);CHECK(xe_mpsc_records_peek(q,&t)==3 && t==a);
 xe_mpsc_records_advance(q,3);CHECK(xe_mpsc_records_peek(q,&t)==2 && t==b);
 xe_mpsc_records_advance(q,2);xe_mpsc_records_commit(q);xe_mpsc_records_await(q,b+2);
 xe_mpsc_records_free(q);
 q=xe_mpsc_records_new(512);CHECK(q);
 for(i=0;i<512;++i){CHECK(xe_mpsc_records_claim(q,1,&a));xe_mpsc_records_publish(q,a,1);}
 for(i=0;i<512;++i){CHECK(xe_mpsc_records_peek(q,&a)==1 && a==i);xe_mpsc_records_advance(q,1);}
 xe_mpsc_records_commit(q);xe_mpsc_records_free(q);
 for(i=0;i<64;++i){
  s.q=xe_mpsc_records_new(1);CHECK(s.q);s.writer=i&1;s.result=-1;
  if(s.writer)CHECK(xe_mpsc_records_claim(s.q,1,&a));
  thread=sthread_create(waiter,&s);CHECK(thread);xe_mpsc_records_stop(s.q);sthread_join(thread);CHECK(s.result==0);xe_mpsc_records_free(s.q);
 }
 run(1,0);run(4,0);run(8,(size_t)-1-47);
 puts("PASS: 156000 variable records, gap publication, complete wrap/high counters, bounded stop/wake and ownership");return 0;
}
