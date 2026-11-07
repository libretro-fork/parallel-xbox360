/* Texture hint transitions/publication. MSVC C89. */
#include "libretro/core_texture_dirty.h"
#include <stdio.h>
#include <stdlib.h>
#include <rthreads/rthreads.h>
#include <rthreads/retro_eventcount.h>
#include <features/features_cpu.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"texture dirty line %u\n",(unsigned)__LINE__);exit(1);}} while(0)
static xe_texture_dirty state;
static retro_atomic_int_t ack;
static retro_eventcount_t changed;
static unsigned payload;
#ifdef XE_TEXTURE_DIRTY_NATIVE
void *xe_dirty_native_new(void);
void xe_dirty_native_free(void *);
unsigned xe_dirty_native_load(void *);
void xe_dirty_native_mark(void *,unsigned);
int xe_dirty_native_refresh(void *);
static void *native;
#endif
static unsigned read_state(void) {
#ifdef XE_TEXTURE_DIRTY_NATIVE
  if(native) return xe_dirty_native_load(native);
#endif
  return xe_texture_dirty_load(&state);
}
static void publish(unsigned mask) {
#ifdef XE_TEXTURE_DIRTY_NATIVE
  if(native) {xe_dirty_native_mark(native,mask);return;}
#endif
  xe_texture_dirty_update_locked(&state,mask,0);
}
static void clear(void) {
#ifdef XE_TEXTURE_DIRTY_NATIVE
  if(native) {CHECK(xe_dirty_native_refresh(native));return;}
#endif
  xe_texture_dirty_update_locked(&state,0,XE_TEXTURE_DIRTY_ALL);
}
static void wait_value(unsigned value,int flag) {
  int key;int64_t start=cpu_features_get_time_usec(),elapsed;
  for(;;) {
    if((flag?(unsigned)retro_atomic_load_acquire_int(&ack):read_state())==value) return;
    key=retro_eventcount_prepare_wait(&changed);
    if((flag?(unsigned)retro_atomic_load_acquire_int(&ack):read_state())==value) {
      retro_eventcount_cancel_wait(&changed);return;
    }
    elapsed=cpu_features_get_time_usec()-start;CHECK(elapsed<2000000);
    retro_eventcount_commit_wait_timeout(&changed,key,2000000-elapsed);
  }
}
static void writer(void *unused) {
  unsigned i,mask;(void)unused;
  for(i=0;i<20000;++i) {
    wait_value(i*2,1);mask=1+i%3;payload=i+1;
    publish(mask);retro_eventcount_notify(&changed);
    wait_value(i*2+1,1);clear();retro_eventcount_notify(&changed);
  }
}
static void concurrent(void) {
  sthread_t *thread;unsigned i,mask;
  retro_atomic_int_init(&ack,0);xe_texture_dirty_init(&state,0);
  CHECK(retro_eventcount_init(&changed));thread=sthread_create(writer,NULL);CHECK(thread);
  for(i=0;i<20000;++i) {
    mask=1+i%3;wait_value(mask,0);CHECK(payload==i+1);
    retro_atomic_store_release_int(&ack,(int)(i*2+1));retro_eventcount_notify(&changed);
    wait_value(0,0);retro_atomic_store_release_int(&ack,(int)(i*2+2));retro_eventcount_notify(&changed);
  }
  sthread_join(thread);retro_eventcount_free(&changed);CHECK(!read_state());
}
int main(void) {
  unsigned initial,set,reset,expected;
  for(initial=0;initial<16;++initial) for(set=0;set<16;++set) for(reset=0;reset<16;++reset) {
    xe_texture_dirty_init(&state,initial);
    CHECK(xe_texture_dirty_load(&state)==(initial&3));
    xe_texture_dirty_update_locked(&state,set,reset);expected=((initial|set)&~reset)&3;
    CHECK(xe_texture_dirty_load(&state)==expected && xe_texture_dirty_load_locked(&state)==expected);
  }
  concurrent();puts("PASS: 4096 masked transitions and 20000 atomic hint publication/clear handshakes");
#ifdef XE_TEXTURE_DIRTY_NATIVE
  native=xe_dirty_native_new();CHECK(native && read_state()==3);
  CHECK(!xe_dirty_native_refresh(native) && read_state()==3);
  xe_dirty_native_mark(native,3);CHECK(xe_dirty_native_refresh(native) && !read_state());
  concurrent();xe_dirty_native_free(native);native=NULL;
  puts("PASS: actual texture watch invalidation/refresh and 20000 cross-thread callback handshakes; no GPU");
#endif
  return 0;
}
