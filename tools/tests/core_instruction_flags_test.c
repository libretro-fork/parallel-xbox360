/* Concurrent monotonic mapped facts, legacy bit ABI; C89/rthreads. */
#include <stdio.h>
#include <string.h>
#include <rthreads/rthreads.h>
#include "core_instruction_flags.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"flag line %d: %s\n",__LINE__,#x); return 1; } } while (0)
struct legacy_flags {
  uint32_t was_resolved:1, accessed_mmio:1, is_syscall_func:1, is_return_site:1, reserved:28;
};
struct shared_state {
  xe_ic_flags flags;
  retro_atomic_int_t start, ready, done, error;
};
struct writer_state { struct shared_state *shared; uint32_t mask; };
static void writer(void *opaque) {
  struct writer_state *state=(struct writer_state *)opaque;
  unsigned i;
  retro_atomic_fetch_add_int(&state->shared->ready,1);
  while (!retro_atomic_load_acquire_int(&state->shared->start)) {}
  for(i=0;i<100000;++i) xe_ic_flags_set(&state->shared->flags,state->mask);
  retro_atomic_fetch_add_int(&state->shared->done,1);
}
static void reader(void *opaque) {
  struct shared_state *state=(struct shared_state *)opaque;
  uint32_t previous=0, current;
  retro_atomic_fetch_add_int(&state->ready,1);
  while (!retro_atomic_load_acquire_int(&state->start)) {}
  do {
    current=xe_ic_flags_load(&state->flags);
    if ((current & previous)!=previous || (current & ~15U))
      retro_atomic_store_relaxed_int(&state->error,1);
    previous=current;
  } while(retro_atomic_load_acquire_int(&state->done)!=4);
}
int main(void) {
  struct legacy_flags old;
  uint32_t mask;
  struct shared_state shared;
  struct writer_state states[4];
  sthread_t *threads[5];
  unsigned i, round;
  size_t old_size=sizeof(old), word_size=sizeof(xe_ic_flags);
  CHECK(old_size==4 && word_size==4);
  for(i=0;i<4;++i) {
    memset(&old,0,sizeof(old));
    switch(i) {
      case 0:old.was_resolved=1;break;
      case 1:old.accessed_mmio=1;break;
      case 2:old.is_syscall_func=1;break;
      default:old.is_return_site=1;break;
    }
    memcpy(&mask,&old,4); CHECK(mask==(1U<<i));
  }
  CHECK(!xe_ic_flags_load(NULL) && !xe_ic_flags_test(NULL,15));
  xe_ic_flags_set(NULL,15);
  for(round=0;round<8;++round) {
    retro_atomic_store_relaxed_int(&shared.flags,0);
    retro_atomic_store_relaxed_int(&shared.start,0);
    retro_atomic_store_relaxed_int(&shared.ready,0);
    retro_atomic_store_relaxed_int(&shared.done,0);
    retro_atomic_store_relaxed_int(&shared.error,0);
    for(i=0;i<4;++i) {
      states[i].shared=&shared; states[i].mask=1U<<i;
      threads[i]=sthread_create(writer,&states[i]); CHECK(threads[i]);
    }
    threads[4]=sthread_create(reader,&shared); CHECK(threads[4]);
    while(retro_atomic_load_acquire_int(&shared.ready)!=5) {}
    retro_atomic_store_release_int(&shared.start,1);
    for(i=0;i<5;++i) sthread_join(threads[i]);
    CHECK(!retro_atomic_load_relaxed_int(&shared.error));
    CHECK(xe_ic_flags_load(&shared.flags)==15 && xe_ic_flags_test(&shared.flags,XE_IC_FLAG_RETURN_SITE));
  }
  retro_atomic_store_relaxed_int(&shared.flags,(int32_t)0x40000000U);
  xe_ic_flags_set(&shared.flags,XE_IC_FLAG_MMIO);
  CHECK(xe_ic_flags_load(&shared.flags)==0x40000002U);
  puts("Instruction flags: legacy 32-bit masks and 3.2M concurrent monotonic OR updates pass");
  return 0;
}
