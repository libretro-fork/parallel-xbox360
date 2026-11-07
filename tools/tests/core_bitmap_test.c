/* C89 tests for production bitmap ownership and common atomic transitions. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <rthreads/rthreads.h>
#include "libretro/core_bitmap.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %u\n",(unsigned)__LINE__); exit(1); } } while (0)
static unsigned reallocations;
static int fail_allocation;
static void *tracked_realloc(void *p, size_t size)
{
   ++reallocations;
   if (fail_allocation) return NULL;
   return realloc(p,size);
}
#define realloc tracked_realloc
#include "../../libretro/core_bitmap.c"
#undef realloc
#define THREADS 8
#define ITERATIONS 50000
static xe_bitmap bitmap;
static retro_atomic_int_t occupied[320];
static uint32_t payload[320];
static void worker(void *opaque)
{
   unsigned id=*(unsigned *)opaque, i;
   size_t index;
   for (i=0;i<ITERATIONS;++i) {
      index=xe_bitmap_acquire(&bitmap,id&1);
      CHECK(index<320);
      CHECK(retro_atomic_cas_int(&occupied[index],0,1));
      payload[index]+=id+1;
      CHECK(retro_atomic_cas_int(&occupied[index],1,0));
      CHECK(xe_bitmap_release(&bitmap,index));
   }
}
static void atomic_transitions(void)
{
   retro_atomic_int_t count, flags;
   retro_atomic_64_t header;
   unsigned char before[8]={0x81,0x23,0x45,0x60,0xFF,0xFF,0xFF,0xFF};
   unsigned char after[8]={0x81,0x23,0x45,0x70,0,0,0,0};
   unsigned char actual[8];
   int64_t old_bits,new_bits,read_bits;
   retro_atomic_int_init(&count,-1);
   CHECK(retro_atomic_fetch_add_int(&count,1)==-1);
   CHECK(retro_atomic_load_relaxed_int(&count)==0);
   CHECK(!retro_atomic_cas_int(&count,-1,0));
   CHECK(retro_atomic_fetch_add_int(&count,1)==0);
   CHECK(retro_atomic_fetch_sub_int(&count,1)==1);
   CHECK(retro_atomic_fetch_sub_int(&count,1)==0);
   CHECK(retro_atomic_cas_int(&count,-1,0));
   retro_atomic_store_release_int(&count,INT_MAX);
   CHECK(retro_atomic_fetch_add_int(&count,1)==INT_MAX);
   CHECK(retro_atomic_load_acquire_int(&count)==INT_MIN);
   CHECK(retro_atomic_fetch_sub_int(&count,1)==INT_MIN);
   CHECK(retro_atomic_load_acquire_int(&count)==INT_MAX);
   retro_atomic_int_init(&flags,0);
   CHECK(retro_atomic_fetch_or_int(&flags,1)==0);
   CHECK(retro_atomic_fetch_or_int(&flags,8)==1);
   CHECK(retro_atomic_load_acquire_int(&flags)==9);
   memcpy(&old_bits,before,8); memcpy(&new_bits,after,8);
   retro_atomic_64_init(&header,old_bits);
   CHECK(!retro_atomic_cas_64(&header,new_bits,0));
   CHECK(retro_atomic_cas_64(&header,old_bits,new_bits));
   read_bits=retro_atomic_load_acquire_64(&header);
   memcpy(actual,&read_bits,8); CHECK(!memcmp(actual,after,8));
   retro_atomic_store_release_64(&header,old_bits);
   CHECK(retro_atomic_load_acquire_64(&header)==old_bits);
}
static void state_roundtrip(void)
{
   unsigned char saved[42], before[40], after[40];
   uint64_t words[5];
   unsigned i, allocations=reallocations;
   xe_bitmap empty={0};
   CHECK(xe_bitmap_capture(&empty,NULL,0));
   CHECK(xe_bitmap_restore(&empty,NULL,0));
   for(i=0;i<320;++i) {
      memset(words,0,sizeof(words));
      words[i/64]=(uint64_t)1<<(63-(i&63));
      memset(saved,0x5A,sizeof(saved));memcpy(saved+1,words,40);
      CHECK(xe_bitmap_restore(&bitmap,saved+1,40));
      CHECK(xe_bitmap_capture(&bitmap,after,40) && !memcmp(after,words,40));
      CHECK(xe_bitmap_acquire(&bitmap,0)==i && xe_bitmap_acquire(&bitmap,0)==SIZE_MAX);
      CHECK(saved[0]==0x5A && saved[41]==0x5A);
   }
   xe_bitmap_reset(&bitmap);
   for(i=0;i<71;++i) CHECK(xe_bitmap_acquire(&bitmap,0)==i);
   CHECK(xe_bitmap_capture(&bitmap,saved+1,40));
   CHECK(xe_bitmap_capture(&bitmap,before,40));
   CHECK(!xe_bitmap_capture(&bitmap,saved+1,39));
   CHECK(!xe_bitmap_restore(&bitmap,saved+1,41));
   CHECK(!xe_bitmap_restore(&bitmap,NULL,40));
   CHECK(!xe_bitmap_capture(&bitmap,NULL,40));
   CHECK(!xe_bitmap_capture(&bitmap,(void *)bitmap.words,40));
   CHECK(!xe_bitmap_capture(&bitmap,(void *)&bitmap,40));
   CHECK(!xe_bitmap_restore(&bitmap,(const void *)&bitmap,40));
   CHECK(!xe_bitmap_restore(&bitmap,(const void *)((uintptr_t)bitmap.words+1),40));
   CHECK(!xe_bitmap_restore(&bitmap,(const void *)(UINTPTR_MAX-7),40));
   CHECK(!xe_bitmap_capture(&bitmap,(void *)(UINTPTR_MAX-7),40));
   CHECK(xe_bitmap_capture(&bitmap,after,40) && !memcmp(before,after,40));
   xe_bitmap_reset(&bitmap);
   CHECK(xe_bitmap_restore(&bitmap,saved+1,40));
   for(i=71;i<320;++i) CHECK(xe_bitmap_acquire(&bitmap,0)==i);
   CHECK(xe_bitmap_acquire(&bitmap,0)==SIZE_MAX);
   CHECK(reallocations==allocations);
   xe_bitmap_reset(&bitmap);
}
int main(void)
{
   sthread_t *threads[THREADS];
   unsigned roles[THREADS],i,old_allocations;
   uint64_t total=0;
   retro_atomic_64_t *stable;
   size_t index;
   atomic_transitions();
   fail_allocation=1;
   CHECK(!xe_bitmap_resize(&bitmap,320));
   CHECK(!bitmap.words && !bitmap.count && !bitmap.capacity);
   fail_allocation=0;
   CHECK(xe_bitmap_resize(&bitmap,320));
   stable=bitmap.words;
   state_roundtrip();
   CHECK(!xe_bitmap_resize(&bitmap,319));
   CHECK(bitmap.words==stable && bitmap.count==5);
   for (i=0;i<320;++i) CHECK(xe_bitmap_acquire(&bitmap,0)==i);
   CHECK(xe_bitmap_acquire(&bitmap,0)==SIZE_MAX);
   CHECK(xe_bitmap_acquire(&bitmap,1)==SIZE_MAX);
   CHECK(!xe_bitmap_release(&bitmap,320));
   for (i=0;i<320;++i) CHECK(xe_bitmap_release(&bitmap,i));
   CHECK(!xe_bitmap_release(&bitmap,0));
   for (i=0;i<320;++i)
      CHECK(xe_bitmap_acquire(&bitmap,1)==(4-i/64)*64+i%64);
   xe_bitmap_reset(&bitmap);
   CHECK(xe_bitmap_acquire(&bitmap,0)==0);
   CHECK(xe_bitmap_resize(&bitmap,64));
   CHECK(xe_bitmap_resize(&bitmap,320));
   CHECK(bitmap.words==stable);
   CHECK(xe_bitmap_acquire(&bitmap,0)==1);
   CHECK(xe_bitmap_acquire(&bitmap,1)==256);
   fail_allocation=1;
   CHECK(!xe_bitmap_resize(&bitmap,32768));
   CHECK(bitmap.words==stable && bitmap.count==5);
   fail_allocation=0;
   xe_bitmap_reset(&bitmap);
   old_allocations=reallocations;
   for (i=0;i<THREADS;++i) { roles[i]=i; threads[i]=sthread_create(worker,&roles[i]); CHECK(threads[i]); }
   for (i=0;i<THREADS;++i) sthread_join(threads[i]);
   CHECK(reallocations==old_allocations && bitmap.words==stable);
   for (i=0;i<320;++i) { CHECK(!retro_atomic_load_relaxed_int(&occupied[i])); total+=payload[i]; }
   CHECK(total==(uint64_t)ITERATIONS*THREADS*(THREADS+1)/2);
   for (i=0;i<320;++i) CHECK(xe_bitmap_acquire(&bitmap,0)==i);
   CHECK(xe_bitmap_acquire(&bitmap,0)==SIZE_MAX);
   CHECK(xe_bitmap_resize(&bitmap,0)); CHECK(xe_bitmap_acquire(&bitmap,1)==SIZE_MAX);
   CHECK(xe_bitmap_resize(&bitmap,32768));
   for (i=0;i<32768;++i) { index=xe_bitmap_acquire(&bitmap,0); CHECK(index==i); }
   CHECK(xe_bitmap_acquire(&bitmap,0)==SIZE_MAX);
   xe_bitmap_close(&bitmap); xe_bitmap_close(&bitmap);
   CHECK(!bitmap.words && !bitmap.count && !bitmap.capacity);
   puts("PASS: 320 quiescent snapshot masks/unaligned restore and invalid-state nonmutation; 400000 concurrent bitmap leases, exact front/back order, cold resize/failure/reset, zero hot reallocations; 32/64-bit CAS and counter/packed-byte transitions");
   return 0;
}
