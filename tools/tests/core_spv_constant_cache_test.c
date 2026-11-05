#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../libretro/core_spv_constant_cache.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
typedef struct record {
   xe_spv_constant_key key;
   uint32_t words[3];
} record;
static size_t allocations, releases, comparisons;
static int fail_allocation;
static void *allocate(size_t bytes)
{
   if (fail_allocation) return NULL;
   ++allocations;
   return malloc(bytes);
}
static void release(void *value)
{
   if (value) ++releases;
   free(value);
}
static int equal(const void *opaque, const xe_spv_constant_key *key)
{
   const record *value = (const record *)opaque;
   ++comparisons;
   return value->key.opcode == key->opcode && value->key.type == key->type &&
      value->key.count == key->count &&
      (!key->count || !memcmp(value->key.words, key->words, key->count * sizeof(uint32_t)));
}
int main(void)
{
   xe_spv_constant_cache cache;
   record values[400];
   xe_spv_constant_slot *old;
   size_t i, pass, calls;
   memset(&cache, 0, sizeof(cache));
   CHECK(!xe_spv_constant_cache_find(&cache, 7, NULL, equal));
   CHECK(!xe_spv_constant_cache_insert_reserved(&cache, 7, values));
   xe_spv_constant_cache_reset(&cache);
   for (i = 0; i < 400; ++i) {
      values[i].key.opcode = (uint32_t)(i % 7);
      values[i].key.type = (uint32_t)i;
      values[i].key.count = i % 4;
      values[i].key.words = values[i].words;
      values[i].words[0] = (uint32_t)i;
      values[i].words[1] = (uint32_t)(i * 1234567);
      values[i].words[2] = ~(uint32_t)i;
      CHECK(xe_spv_constant_cache_reserve(&cache, i + 1, allocate, release));
      /* Deliberately collide every record, including wraparound probing. */
      CHECK(xe_spv_constant_cache_insert_reserved(&cache, 63, values + i));
   }
   for (i = 0; i < 400; ++i)
      CHECK(xe_spv_constant_cache_find(&cache, 63, &values[i].key, equal) == values + i);
   CHECK(!xe_spv_constant_cache_insert_reserved(&cache, 0, NULL));
   old = cache.slots;
   fail_allocation = 1;
   CHECK(!xe_spv_constant_cache_reserve(&cache, cache.capacity, allocate, release));
   CHECK(cache.slots == old && cache.count == 400);
   CHECK(!xe_spv_constant_cache_reserve(&cache, (size_t)-1, allocate, release));
   fail_allocation = 0;
   calls = allocations;
   for (pass = 0; pass < 200; ++pass) {
      xe_spv_constant_cache_reset(&cache);
      comparisons = 0;
      CHECK(!xe_spv_constant_cache_find(&cache, 63, &values[0].key, equal));
      CHECK(!comparisons);
      for (i = 0; i < 400; ++i) {
         uint32_t hash = xe_spv_constant_hash(&values[i].key);
         CHECK(xe_spv_constant_cache_reserve(&cache, i + 1, allocate, release));
         CHECK(xe_spv_constant_cache_insert_reserved(&cache, hash, values + i));
         CHECK(xe_spv_constant_cache_find(&cache, hash, &values[i].key, equal) == values + i);
      }
   }
   CHECK(allocations == calls);
   cache.epoch = UINT32_MAX;
   xe_spv_constant_cache_reset(&cache);
   CHECK(cache.epoch == 1 && !cache.count);
   comparisons = 0;
   CHECK(!xe_spv_constant_cache_find(&cache, 63, &values[0].key, equal) && !comparisons);
   release(cache.slots);
   CHECK(allocations == releases);
   puts("PASS: constant-cache collisions, epoch/wrap reset, 80000 warm inserts and checked growth failure");
   return 0;
}
