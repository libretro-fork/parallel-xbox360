/* Pipeline publication and warm-up retry ordering. MSVC C89. */
#include "libretro/core_pipeline_publication.h"
#include <stdio.h>
#include <stdlib.h>
#include <rthreads/rthreads.h>
#include <rthreads/retro_eventcount.h>
#include <features/features_cpu.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"pipeline publication line %u\n",(unsigned)__LINE__);exit(1);}} while(0)
static xe_pipeline_publication state;
static retro_atomic_int_t request,ack,done;
static retro_eventcount_t changed;
static unsigned payload;
static int concurrent_phase;
static void wait_value(retro_atomic_int_t *value,unsigned expected) {
  int key;int64_t start=cpu_features_get_time_usec(),elapsed;
  while((unsigned)retro_atomic_load_acquire_int(value)!=expected) {
    key=retro_eventcount_prepare_wait(&changed);
    if((unsigned)retro_atomic_load_acquire_int(value)==expected){retro_eventcount_cancel_wait(&changed);return;}
    elapsed=cpu_features_get_time_usec()-start;CHECK(elapsed<2000000);
    retro_eventcount_commit_wait_timeout(&changed,key,2000000-elapsed);
  }
}
/* Called only by the external negative header, never by release code. */
void xe_pipeline_negative_failure_window(void) {
  if(concurrent_phase) {
    retro_eventcount_notify(&changed);
    wait_value(&ack,(unsigned)retro_atomic_load_acquire_int(&request));
  }
}
static void creator(void *unused) {
  unsigned i;(void)unused;
  for(i=1;i<=20000;++i) {
    wait_value(&request,i);payload=i;
    xe_pipeline_publish_failure(&state);
    /* No old-entry access after granting a retry. Only fixture bookkeeping. */
    retro_eventcount_notify(&changed);wait_value(&ack,i);
    retro_atomic_store_release_int(&done,(int)i);retro_eventcount_notify(&changed);
  }
}
static void retry_test(void) {
  unsigned i;int key;int64_t start,elapsed;sthread_t *thread;
  retro_atomic_int_init(&request,0);retro_atomic_int_init(&ack,0);retro_atomic_int_init(&done,0);
  CHECK(retro_eventcount_init(&changed));concurrent_phase=1;
  thread=sthread_create(creator,NULL);CHECK(thread);
  for(i=1;i<=20000;++i) {
    xe_pipeline_publication_init(&state);
    xe_pipeline_flag_store(&state,XE_PIPELINE_PENDING|XE_PIPELINE_PLACEHOLDER|XE_PIPELINE_INTERPRETER,1);
    retro_atomic_store_release_int(&request,(int)i);retro_eventcount_notify(&changed);
    start=cpu_features_get_time_usec();
    while(!xe_pipeline_flag_load(&state,XE_PIPELINE_FAILED)) {
      key=retro_eventcount_prepare_wait(&changed);
      if(xe_pipeline_flag_load(&state,XE_PIPELINE_FAILED)){retro_eventcount_cancel_wait(&changed);break;}
      elapsed=cpu_features_get_time_usec()-start;CHECK(elapsed<2000000);
      retro_eventcount_commit_wait_timeout(&changed,key,2000000-elapsed);
    }
    CHECK(payload==i);
    CHECK(xe_pipeline_flags_load(&state)==(XE_PIPELINE_FAILED|XE_PIPELINE_INTERPRETER));
    /* Begin a retry as soon as failure is visible, before creator returns. */
    xe_pipeline_flag_store(&state,XE_PIPELINE_PENDING,1);
    retro_atomic_store_release_int(&ack,(int)i);retro_eventcount_notify(&changed);wait_value(&done,i);
    CHECK(xe_pipeline_flag_load(&state,XE_PIPELINE_PENDING));
  }
  sthread_join(thread);concurrent_phase=0;retro_eventcount_free(&changed);
}
static void transitions(void) {
  xe_pipeline_publication p,copy;xe_pipeline_draw draw,saved;unsigned mask,bit;
  for(mask=0;mask<16;++mask) {
    xe_pipeline_publication_init(&p);draw=xe_pipeline_for_draw(&p);
    CHECK(!draw.handle && !draw.placeholder && !draw.interpreter);
    xe_pipeline_flag_store(&p,mask,1);CHECK(xe_pipeline_flags_load(&p)==mask);
    xe_pipeline_placeholder_store(&p,UINT64_C(0x1234567800001000));
    xe_pipeline_handle_store(&p,UINT64_C(0x1234567800001000));
    draw=xe_pipeline_for_draw(&p);CHECK(draw.placeholder && draw.interpreter==!!(mask&XE_PIPELINE_INTERPRETER));
    xe_pipeline_publication_copy(&copy,&p);
    CHECK(xe_pipeline_handle_load(&copy)==draw.handle && xe_pipeline_placeholder_load(&copy)==draw.handle);
    CHECK(xe_pipeline_flags_load(&copy)==mask);saved=draw;
    CHECK(xe_pipeline_handle_exchange(&p,UINT64_C(0xfedcba9800002000))==saved.handle);
    draw=xe_pipeline_for_draw(&p);CHECK(draw.handle==UINT64_C(0xfedcba9800002000) && !draw.placeholder && !draw.interpreter);
    CHECK(saved.placeholder && saved.handle==UINT64_C(0x1234567800001000));
    for(bit=1;bit<=8;bit<<=1) {
      xe_pipeline_flag_store(&p,bit,1);CHECK(xe_pipeline_flag_load(&p,bit));
      xe_pipeline_flag_store(&p,bit,0);CHECK(!xe_pipeline_flag_load(&p,bit));
    }
    xe_pipeline_handle_store(&p,xe_pipeline_placeholder_load(&p));
    xe_pipeline_flag_store(&p,XE_PIPELINE_PLACEHOLDER|XE_PIPELINE_INTERPRETER|XE_PIPELINE_PENDING,1);
    xe_pipeline_publish_failure(&p);draw=xe_pipeline_for_draw(&p);
    CHECK(draw.placeholder && draw.interpreter); /* Physical fallback remains. */
    CHECK(xe_pipeline_flags_load(&p)==(XE_PIPELINE_FAILED|XE_PIPELINE_INTERPRETER));
  }
}
static retro_atomic_int_t flag_phase,flags_set,flags_cleared;
static void flag_writer(void *opaque) {
  unsigned i,bit=*(unsigned *)opaque;
  for(i=0;i<2000;++i) {
    wait_value(&flag_phase,i*2+1);xe_pipeline_flag_store(&state,bit,1);
    retro_atomic_fetch_add_int(&flags_set,1);retro_eventcount_notify(&changed);
    wait_value(&flag_phase,i*2+2);xe_pipeline_flag_store(&state,bit,0);
    retro_atomic_fetch_add_int(&flags_cleared,1);retro_eventcount_notify(&changed);
  }
}
static void independent_flags(void) {
  unsigned i,j,bits[4];sthread_t *threads[4];
  xe_pipeline_publication_init(&state);retro_atomic_int_init(&flag_phase,0);
  retro_atomic_int_init(&flags_set,0);retro_atomic_int_init(&flags_cleared,0);
  CHECK(retro_eventcount_init(&changed));
  for(j=0;j<4;++j){bits[j]=1u<<j;threads[j]=sthread_create(flag_writer,&bits[j]);CHECK(threads[j]);}
  for(i=0;i<2000;++i) {
    retro_atomic_store_release_int(&flag_phase,(int)(i*2+1));retro_eventcount_notify(&changed);
    wait_value(&flags_set,(i+1)*4);CHECK(xe_pipeline_flags_load(&state)==15);
    retro_atomic_store_release_int(&flag_phase,(int)(i*2+2));retro_eventcount_notify(&changed);
    wait_value(&flags_cleared,(i+1)*4);CHECK(!xe_pipeline_flags_load(&state));
  }
  for(j=0;j<4;++j) sthread_join(threads[j]);
  retro_eventcount_free(&changed);
}
#ifdef XE_PIPELINE_NATIVE_TEST
void *xe_pipeline_native_new(unsigned);
void xe_pipeline_native_free(unsigned,void *);
xe_pipeline_publication *xe_pipeline_native_state(unsigned,void *);
uint64_t xe_pipeline_native_draw(unsigned,void *,unsigned *);
int xe_pipeline_native_clone(unsigned,void *);
unsigned xe_pipeline_native_size(unsigned);
static void native_tests(void) {
  unsigned backend,flags,first=0;void *entry;xe_pipeline_publication *p;
#ifdef XE_PIPELINE_VULKAN_ONLY
  first=1;
#endif
  for(backend=first;backend<2;++backend) {
    entry=xe_pipeline_native_new(backend);CHECK(entry);p=xe_pipeline_native_state(backend,entry);
    CHECK(!xe_pipeline_native_draw(backend,entry,&flags) && !flags);
    xe_pipeline_flag_store(p,XE_PIPELINE_PLACEHOLDER|XE_PIPELINE_INTERPRETER|XE_PIPELINE_PENDING,1);
    xe_pipeline_placeholder_store(p,4096);xe_pipeline_handle_store(p,4096);
    CHECK(xe_pipeline_native_draw(backend,entry,&flags)==4096 && flags==3);
    CHECK(xe_pipeline_native_clone(backend,entry));
    CHECK(xe_pipeline_handle_exchange(p,8192)==4096);
    CHECK(xe_pipeline_native_draw(backend,entry,&flags)==8192 && !flags);
    CHECK(xe_pipeline_native_clone(backend,entry));
    printf("native backend %u entry %u bytes; publication %lu bytes\n",backend,xe_pipeline_native_size(backend),(unsigned long)sizeof(*p));
    xe_pipeline_native_free(backend,entry);
  }
}
#endif
int main(void) {
  transitions();retry_test();independent_flags();
#ifdef XE_PIPELINE_NATIVE_TEST
  native_tests();
#endif
  puts("PASS: concrete placeholder snapshots, 64-bit handles, copy/publication 20000 immediate warm-up retries and four independent flag writers");
  return 0;
}
