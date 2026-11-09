#include <retro_atomic.h>
#include <rthreads/rthreads.h>
#include <rthreads/retro_eventcount.h>
#include <features/features_cpu.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void*bench_new(unsigned,size_t,unsigned char*);void bench_free(void*);size_t bench_bytes(void*);size_t bench_claim(void*,size_t);void bench_publish(void*,size_t,size_t,unsigned);size_t bench_peek(void*,size_t*);void bench_advance(void*,size_t);void bench_commit(void*);
#define CHECK(x) do{if(!(x)){fprintf(stderr,"bench line%u\n",(unsigned)__LINE__);exit(1);}}while(0)
#ifndef BENCH_CAP
#define BENCH_CAP 512
#endif
#ifndef BENCH_UNIT
#define BENCH_UNIT 16
#endif
#define CAP BENCH_CAP
struct state{void*q;unsigned char*bytes;unsigned id,records,wake,span;retro_atomic_int_t*start,*ready;retro_eventcount_t*event;};
struct header{size_t span;unsigned producer,index;};
static void worker(void*opaque){struct state*s=(struct state*)opaque;unsigned i;size_t t,off,n,first;struct header h;int key;
 retro_atomic_fetch_add_int(s->ready,1);retro_eventcount_notify(s->event);
 while(!retro_atomic_load_acquire_int(s->start)){key=retro_eventcount_prepare_wait(s->event);if(!retro_atomic_load_acquire_int(s->start))retro_eventcount_commit_wait(s->event,key);else retro_eventcount_cancel_wait(s->event);}
 for(i=0;i<s->records;++i){n=s->span;t=bench_claim(s->q,n);off=(t&(CAP-1))*BENCH_UNIT;first=CAP*BENCH_UNIT-off;if(first>n*BENCH_UNIT)first=n*BENCH_UNIT;
 memset(s->bytes+off,(unsigned char)i,first);memset(s->bytes,(unsigned char)i,n*BENCH_UNIT-first);
 h.span=n;h.producer=s->id;h.index=i;memcpy(s->bytes+off,&h,sizeof(h));bench_publish(s->q,t,n,s->wake);}}
int main(int argc,char**argv){unsigned current,workers,span,records,wake,round,i,seen[8],done,batch;unsigned char*bytes;void*q;struct state states[8];sthread_t*threads[8];retro_atomic_int_t start,ready;retro_eventcount_t event;size_t ticket,n,off;int key;int64_t begin,end;struct header h;
 CHECK(argc==6);current=atoi(argv[1]);workers=atoi(argv[2]);span=atoi(argv[3]);records=atoi(argv[4]);wake=atoi(argv[5]);CHECK(workers&&workers<=8&&span<=CAP);
 bytes=(unsigned char*)malloc(CAP*BENCH_UNIT);CHECK(bytes);q=bench_new(current,CAP,bytes);CHECK(q);CHECK(retro_eventcount_init(&event));
 printf("STORAGE,%u,%llu\n",current,(unsigned long long)bench_bytes(q));
 for(round=0;round<5;++round){memset(seen,0,sizeof(seen));retro_atomic_int_init(&start,0);retro_atomic_int_init(&ready,0);
 for(i=0;i<workers;++i){states[i].q=q;states[i].bytes=bytes;states[i].id=i;states[i].records=records;states[i].span=span;states[i].wake=wake;states[i].start=&start;states[i].ready=&ready;states[i].event=&event;threads[i]=sthread_create(worker,states+i);CHECK(threads[i]);}
 while(retro_atomic_load_acquire_int(&ready)!=(int)workers){key=retro_eventcount_prepare_wait(&event);if(retro_atomic_load_acquire_int(&ready)!=(int)workers)retro_eventcount_commit_wait(&event,key);else retro_eventcount_cancel_wait(&event);}
 done=batch=0;begin=cpu_features_get_time_usec();retro_atomic_store_release_int(&start,1);retro_eventcount_notify(&event);
 while(done<workers*records){n=bench_peek(q,&ticket);if(!n){bench_commit(q);batch=0;continue;}
 off=(ticket&(CAP-1))*BENCH_UNIT;memcpy(&h,bytes+off,sizeof(h));CHECK(h.span==n&&h.producer<workers&&h.index==seen[h.producer]++);if(n*BENCH_UNIT>sizeof(h))CHECK(bytes[(off+n*BENCH_UNIT-1)%(CAP*BENCH_UNIT)]==(unsigned char)h.index);
 bench_advance(q,n);++done;if(++batch==64){bench_commit(q);batch=0;}}
 bench_commit(q);end=cpu_features_get_time_usec();for(i=0;i<workers;++i){sthread_join(threads[i]);CHECK(seen[i]==records);}
 printf("SAMPLE,%u,%u,%u,%u,%u,%lld\n",current,workers,span,wake,round,(long long)(end-begin));}
 retro_eventcount_free(&event);bench_free(q);free(bytes);return 0;}
