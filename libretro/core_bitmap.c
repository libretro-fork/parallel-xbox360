/* Copyright 2015 Ben Vanik. All rights reserved.
 * Released under the BSD license - see LICENSE in the repository root. */
#include "core_bitmap.h"
#include <stdint.h>
#include <stdlib.h>
#include <compat/intrinsics.h>
#if !defined(RETRO_ATOMIC_HAS_64) || !defined(RETRO_ATOMIC_LOCK_FREE)
#error Lock-free 64-bit common atomics are required.
#endif
int xe_bitmap_resize(xe_bitmap *map, size_t bits)
{
   size_t count, i;
   retro_atomic_64_t *words;
   if (!map || (bits & 63)) return 0;
   count=bits/64;
   if (count>SIZE_MAX/sizeof(*words)) return 0;
   if (count>map->capacity) {
      words=(retro_atomic_64_t *)realloc((void *)map->words,count*sizeof(*words));
      if (!words) return 0;
      map->words=words; map->capacity=count;
   }
   for (i=map->count;i<count;++i) retro_atomic_64_init(&map->words[i],-1);
   map->count=count;
   return 1;
}
void xe_bitmap_reset(xe_bitmap *map)
{
   size_t i;
   for (i=0;i<map->count;++i) retro_atomic_store_relaxed_64(&map->words[i],-1);
}
size_t xe_bitmap_acquire(xe_bitmap *map, int from_back)
{
   size_t n, word;
   uint64_t bits, desired;
   uint32_t index;
   for (n=0;n<map->count;++n) {
      word=from_back ? map->count-1-n : n;
      for (;;) {
         bits=(uint64_t)retro_atomic_load_relaxed_64(&map->words[word]);
         if (!bits) break;
         index=compat_clz_u64(bits);
         desired=bits & ~((uint64_t)1<<(63-index));
         if (retro_atomic_cas_64(&map->words[word],(int64_t)bits,(int64_t)desired))
            return word*64+index;
      }
   }
   return SIZE_MAX;
}
int xe_bitmap_release(xe_bitmap *map, size_t index)
{
   uint64_t bits, mask;
   size_t word=index/64;
   if (word>=map->count) return 0;
   mask=(uint64_t)1<<(63-(index&63));
   for (;;) {
      bits=(uint64_t)retro_atomic_load_relaxed_64(&map->words[word]);
      if (bits&mask) return 0;
      if (retro_atomic_cas_64(&map->words[word],(int64_t)bits,(int64_t)(bits|mask)))
         return 1;
   }
}
void xe_bitmap_close(xe_bitmap *map)
{
   if (!map) return;
   free((void *)map->words); map->words=NULL; map->count=map->capacity=0;
}
