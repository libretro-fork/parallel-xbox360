/* Publication scenarios stay C89; optional bridge exercises native owners. */
#include "libretro/core_binary_slot.h"
#include <rthreads/rthreads.h>
#include <rthreads/retro_eventcount.h>
#include <features/features_cpu.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"binary slot line %u\n",(unsigned)__LINE__); abort(); } } while (0)
typedef struct payload { unsigned tag; unsigned char bytes[64]; } payload;
static xe_binary_slot shared;
static retro_atomic_int_t arrived,go,winners,allocated,released;
static retro_eventcount_t gate;
static unsigned mode;
static void wait_start(void) {
   int key;
   retro_atomic_fetch_add_int(&arrived,1);
   retro_eventcount_notify(&gate);
   while(!retro_atomic_load_acquire_int(&go)) {
      key=retro_eventcount_prepare_wait(&gate);
      if(retro_atomic_load_acquire_int(&go)) {retro_eventcount_cancel_wait(&gate);break;}
      retro_eventcount_commit_wait_timeout(&gate,key,1000000);
   }
}
static void destroy(payload *p) { if(p) {free(p);retro_atomic_fetch_add_int(&released,1);} }
static void producer(void *opaque) {
   unsigned id=*(const unsigned *)opaque,i;
   payload *p=NULL;
   if(mode==0 || (mode==2 && (id&1))) {
      p=(payload *)malloc(sizeof(*p));CHECK(p);
      p->tag=id;for(i=0;i<sizeof(p->bytes);++i)p->bytes[i]=(unsigned char)(id*13+i);
      retro_atomic_fetch_add_int(&allocated,1);
   }
   wait_start();
   if(xe_binary_slot_publish(&shared,p))retro_atomic_fetch_add_int(&winners,1);
   else destroy(p);
}
static void reader(void *unused) {
   xe_binary_snapshot snapshot;
   const payload *p;
   unsigned i;
   int64_t start;
   (void)unused;wait_start();start=cpu_features_get_time_usec();
   do {
      snapshot=xe_binary_slot_read(&shared);
      CHECK(cpu_features_get_time_usec()-start<5000000);
   } while(!snapshot.ready);
   if(snapshot.binary) {
      p=(const payload *)snapshot.binary;
      CHECK(p->tag>=1 && p->tag<=4);
      for(i=0;i<sizeof(p->bytes);++i)CHECK(p->bytes[i]==(unsigned char)(p->tag*13+i));
   } else CHECK(mode!=0);
   CHECK(xe_binary_slot_read(&shared).binary==snapshot.binary);
}
static void concurrency(void) {
   unsigned round,i,ids[4]={1,2,3,4};
   sthread_t *threads[8];
   int64_t start;
   CHECK(retro_eventcount_init(&gate));
   for(mode=0;mode<3;++mode)for(round=0;round<20;++round) {
      xe_binary_slot_init(&shared);
      retro_atomic_int_init(&arrived,0);retro_atomic_int_init(&go,0);
      retro_atomic_int_init(&winners,0);retro_atomic_int_init(&allocated,0);retro_atomic_int_init(&released,0);
      for(i=0;i<4;++i){threads[i]=sthread_create(producer,ids+i);CHECK(threads[i]);}
      for(i=4;i<8;++i){threads[i]=sthread_create(reader,NULL);CHECK(threads[i]);}
      start=cpu_features_get_time_usec();
      while(retro_atomic_load_acquire_int(&arrived)!=8)CHECK(cpu_features_get_time_usec()-start<5000000);
      retro_atomic_store_release_int(&go,1);retro_eventcount_notify(&gate);
      for(i=0;i<8;++i)sthread_join(threads[i]);
      CHECK(retro_atomic_load_acquire_int(&winners)==1);
      destroy((payload *)xe_binary_slot_take(&shared));
      CHECK(!xe_binary_slot_take(&shared));
      CHECK(!xe_binary_slot_read(&shared).ready);
      CHECK(retro_atomic_load_acquire_int(&allocated)==retro_atomic_load_acquire_int(&released));
   }
   retro_eventcount_free(&gate);
}
#ifdef XE_BINARY_NATIVE
void *xe_binary_native_shader(unsigned);
void xe_binary_native_sizes(size_t *,size_t *);
void *xe_binary_native_translation(void *,uint64_t);
xe_binary_snapshot xe_binary_native_read(const void *);
int xe_binary_native_publish(void *,void *);
void xe_binary_native_remove(void *,uint64_t);
void xe_binary_native_close(void *);
void *xe_binary_native_create(unsigned);
int xe_binary_native_validate(const void *,unsigned);
void xe_binary_native_delete(void *);
#ifdef XE_SLOT_ASAN
int __asan_address_is_poisoned(const volatile void *);
#endif
typedef struct native_job { void *translation; unsigned id; } native_job;
static void native_producer(void *opaque) {
   native_job *job=(native_job *)opaque;
   void *binary=xe_binary_native_create(job->id);
   CHECK(binary);wait_start();
   if(xe_binary_native_publish(job->translation,binary))retro_atomic_fetch_add_int(&winners,1);
   else {xe_binary_native_delete(binary);retro_atomic_fetch_add_int(&released,1);}
}
static void native_reader(void *opaque) {
   xe_binary_snapshot value;
   unsigned i,valid=0;
   int64_t start;
   wait_start();start=cpu_features_get_time_usec();
   do {
      value=xe_binary_native_read(opaque);
      CHECK(cpu_features_get_time_usec()-start<5000000);
   } while(!value.ready);
   CHECK(value.binary);
   for(i=1;i<=4;++i)valid+=xe_binary_native_validate(value.binary,i)!=0;
   CHECK(valid==1);
}
static void native_concurrency(void) {
   native_job jobs[4];
   sthread_t *threads[8];
   void *shader,*translation;
#ifdef XE_SLOT_ASAN
   void *borrow;
#endif
   unsigned stage,i;
   int64_t start;
   CHECK(retro_eventcount_init(&gate));
   for(stage=0;stage<2;++stage) {
      shader=xe_binary_native_shader(stage);CHECK(shader);
      translation=xe_binary_native_translation(shader,37);CHECK(translation);
      retro_atomic_int_init(&arrived,0);retro_atomic_int_init(&go,0);
      retro_atomic_int_init(&winners,0);retro_atomic_int_init(&released,0);
      for(i=0;i<4;++i) {
         jobs[i].translation=translation;jobs[i].id=i+1;
         threads[i]=sthread_create(native_producer,jobs+i);CHECK(threads[i]);
      }
      for(i=4;i<8;++i){threads[i]=sthread_create(native_reader,translation);CHECK(threads[i]);}
      start=cpu_features_get_time_usec();
      while(retro_atomic_load_acquire_int(&arrived)!=8)CHECK(cpu_features_get_time_usec()-start<5000000);
      retro_atomic_store_release_int(&go,1);retro_eventcount_notify(&gate);
      for(i=0;i<8;++i)sthread_join(threads[i]);
      CHECK(retro_atomic_load_acquire_int(&winners)==1 && retro_atomic_load_acquire_int(&released)==3);
#ifdef XE_SLOT_ASAN
      borrow=xe_binary_native_read(translation).binary;
      CHECK(!__asan_address_is_poisoned(borrow));
#endif
      xe_binary_native_close(shader);
#ifdef XE_SLOT_ASAN
      CHECK(__asan_address_is_poisoned(borrow));
#endif
   }
   retro_eventcount_free(&gate);
   puts("PASS: concurrent real translation borrowers and loser cleanup before joined destruction");
}
static void native_owners(void) {
   void *shader,*a,*b,*c,*binary,*loser;
   unsigned stage;
   size_t base_size,native_size;
   xe_binary_native_sizes(&base_size,&native_size);
   printf("translation bytes: base=%u native=%u slot=%u\n",(unsigned)base_size,(unsigned)native_size,(unsigned)sizeof(xe_binary_slot));
   for(stage=0;stage<2;++stage) {
      shader=xe_binary_native_shader(stage);CHECK(shader);
      a=xe_binary_native_translation(shader,1);
      b=xe_binary_native_translation(shader,UINT64_C(0x8000000000000001));
      c=xe_binary_native_translation(shader,0);
      CHECK(a && b && c && a!=b && a!=c && b!=c);
      CHECK(xe_binary_native_translation(shader,1)==a);
      CHECK(!xe_binary_native_read(a).ready);
      binary=xe_binary_native_create(23);loser=xe_binary_native_create(41);
      CHECK(binary && loser && xe_binary_native_publish(a,binary));
      CHECK(!xe_binary_native_publish(a,loser));xe_binary_native_delete(loser);
      CHECK(xe_binary_native_read(a).ready && xe_binary_native_read(a).binary==binary);
      CHECK(xe_binary_native_validate(xe_binary_native_read(a).binary,23));
      CHECK(xe_binary_native_publish(b,NULL));
      CHECK(xe_binary_native_read(b).ready && !xe_binary_native_read(b).binary);
      CHECK(!xe_binary_native_read(c).ready);
      CHECK(xe_binary_native_read(a).binary==binary);
      xe_binary_native_remove(shader,1);
#ifdef XE_SLOT_ASAN
      CHECK(__asan_address_is_poisoned(binary));
#endif
      a=xe_binary_native_translation(shader,1);
      CHECK(!xe_binary_native_read(a).ready);
      binary=xe_binary_native_create(59);CHECK(binary && xe_binary_native_publish(a,binary));
      xe_binary_native_close(shader);
#ifdef XE_SLOT_ASAN
      CHECK(__asan_address_is_poisoned(binary));
#endif
   }
   puts("PASS: native vertex/pixel owners, exact modification identity, borrowed bytes and virtual destruction");
}
#endif
int main(void) {
   xe_binary_slot slot;
   xe_binary_snapshot empty;
   unsigned binary=7;
   empty=xe_binary_slot_read(NULL);CHECK(!empty.ready && !empty.binary);
   CHECK(!xe_binary_slot_publish(NULL,&binary) && !xe_binary_slot_take(NULL));
   xe_binary_slot_init(&slot);
   CHECK(!xe_binary_slot_publish(&slot,&slot));
   CHECK(xe_binary_slot_publish(&slot,&binary));
   CHECK(xe_binary_slot_read(&slot).binary==&binary);
   CHECK(!xe_binary_slot_publish(&slot,NULL));
   CHECK(xe_binary_slot_take(&slot)==&binary && !xe_binary_slot_take(&slot));
   CHECK(xe_binary_slot_publish(&slot,NULL));
   CHECK(xe_binary_slot_read(&slot).ready && !xe_binary_slot_read(&slot).binary);
   CHECK(!xe_binary_slot_publish(&slot,&binary));
   CHECK(!xe_binary_slot_take(&slot) && !xe_binary_slot_read(&slot).ready);
   concurrency();
#ifdef XE_BINARY_NATIVE
   native_owners();native_concurrency();
#endif
   puts("PASS: 60 success/failure/mixed publication races, acquire-visible bytes and exact-once ownership");
   return 0;
}
