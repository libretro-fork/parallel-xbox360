/* Copyright 2015 Ben Vanik. All rights reserved.
 * Released under the BSD license - see LICENSE in the repository root. */
#include "core_bitmap.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <compat/intrinsics.h>
#if !defined(RETRO_ATOMIC_HAS_64) || !defined(RETRO_ATOMIC_LOCK_FREE)
#error Lock-free 64-bit common atomics are required.
#endif
typedef char bitmap_atomic_word_must_be_eight_bytes[(sizeof(retro_atomic_64_t)==8)?1:-1];
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

static int bitmap_state_range(const xe_bitmap *map, const void *data, size_t bytes)
{
   uintptr_t storage, external, owner;
   if (!map || map->count > map->capacity || map->count > SIZE_MAX / 8 ||
         bytes != map->count * 8 || (bytes && (!map->words || !data))) return 0;
   if (!bytes) return 1;
   storage = (uintptr_t)map->words; external = (uintptr_t)data;
   /* Output must not overwrite the map descriptor while it is being read. */
   owner = (uintptr_t)map;
   if (external >= owner ? external - owner < sizeof(*map) :
         owner - external < bytes) return 0;
   if (storage > UINTPTR_MAX - bytes || external > UINTPTR_MAX - bytes) return 0;
   return storage >= external ? storage - external >= bytes :
         external - storage >= bytes;
}
int xe_bitmap_capture(const xe_bitmap *map, void *out, size_t bytes)
{
   size_t i;
   int64_t value;
   unsigned char *destination = (unsigned char *)out;
   if (!bitmap_state_range(map, out, bytes)) return 0;
   for (i = 0; i < map->count; ++i) {
      value = retro_atomic_load_acquire_64(&map->words[i]);
      memcpy(destination + i * 8, &value, 8);
   }
   return 1;
}
int xe_bitmap_restore(xe_bitmap *map, const void *input, size_t bytes)
{
   size_t i;
   int64_t value;
   const unsigned char *source = (const unsigned char *)input;
   if (!bitmap_state_range(map, input, bytes)) return 0;
   for (i = 0; i < map->count; ++i) {
      memcpy(&value, source + i * 8, 8);
      retro_atomic_store_release_64(&map->words[i], value);
   }
   return 1;
}
