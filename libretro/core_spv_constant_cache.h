/* Exclusive compiler cache. Keys borrow immutable instruction operands; only
 * hash/record metadata moves on growth. Epoch reset never reads retired records. */
#ifndef XE_CORE_SPV_CONSTANT_CACHE_H
#define XE_CORE_SPV_CONSTANT_CACHE_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <retro_inline.h>
typedef struct xe_spv_constant_key {
   uint32_t opcode, type;
   const uint32_t *words;
   size_t count;
} xe_spv_constant_key;
typedef struct xe_spv_constant_slot {
   const void *record;
   uint32_t hash, epoch;
} xe_spv_constant_slot;
typedef struct xe_spv_constant_cache {
   xe_spv_constant_slot *slots;
   size_t count, capacity;
   uint32_t epoch;
} xe_spv_constant_cache;
static INLINE uint32_t xe_spv_constant_hash(const xe_spv_constant_key *key)
{
   uint32_t hash = (UINT32_C(2166136261) ^ key->opcode) * UINT32_C(16777619);
   size_t i;
   hash = (hash ^ key->type) * UINT32_C(16777619);
   hash = (hash ^ (uint32_t)key->count) * UINT32_C(16777619);
   for (i = 0; i < key->count; ++i)
      hash = (hash ^ key->words[i]) * UINT32_C(16777619);
   hash ^= hash >> 16;
   hash *= UINT32_C(0x85ebca6b);
   hash ^= hash >> 13;
   return hash;
}
static INLINE void xe_spv_constant_cache_reset(xe_spv_constant_cache *cache)
{
   ++cache->epoch;
   if (!cache->epoch) {
      if (cache->capacity) memset(cache->slots, 0, cache->capacity * sizeof(*cache->slots));
      cache->epoch = 1;
   }
   cache->count = 0;
}
static INLINE const void *xe_spv_constant_cache_find(
      const xe_spv_constant_cache *cache, uint32_t hash,
      const xe_spv_constant_key *key,
      int (*equal)(const void *, const xe_spv_constant_key *))
{
   size_t slot, remaining;
   if (!cache->capacity || !cache->epoch) return NULL;
   slot = hash & (cache->capacity - 1);
   for (remaining = cache->capacity; remaining; --remaining) {
      const xe_spv_constant_slot *entry = cache->slots + slot;
      if (entry->epoch != cache->epoch) return NULL;
      if (entry->hash == hash && equal(entry->record, key)) return entry->record;
      slot = (slot + 1) & (cache->capacity - 1);
   }
   return NULL;
}
static INLINE int xe_spv_constant_cache_reserve(xe_spv_constant_cache *cache,
      size_t count, void *(*allocate)(size_t), void (*release)(void *))
{
   size_t capacity = cache->capacity ? cache->capacity : 64;
   size_t i, slot, limit = (size_t)-1 / sizeof(*cache->slots);
   xe_spv_constant_slot *slots;
   if (count <= cache->capacity - cache->capacity / 4) return 1;
   while (count > capacity - capacity / 4) {
      if (capacity > limit / 2) return 0;
      capacity *= 2;
   }
   if (capacity > limit) return 0;
   slots = (xe_spv_constant_slot *)allocate(capacity * sizeof(*slots));
   if (!slots) return 0;
   memset(slots, 0, capacity * sizeof(*slots));
   for (i = 0; i < cache->capacity; ++i) {
      const xe_spv_constant_slot *old = cache->slots + i;
      if (old->epoch != cache->epoch) continue;
      slot = old->hash & (capacity - 1);
      while (slots[slot].epoch == cache->epoch) slot = (slot + 1) & (capacity - 1);
      slots[slot] = *old;
   }
   release(cache->slots);
   cache->slots = slots;
   cache->capacity = capacity;
   return 1;
}
/* Caller first reserves, then completes throwing ID publication. No allocation
 * or user callback remains when borrowing the completed record here. */
static INLINE int xe_spv_constant_cache_insert_reserved(
      xe_spv_constant_cache *cache, uint32_t hash, const void *record)
{
   size_t slot;
   if (!record || !cache->epoch || cache->count >= cache->capacity - cache->capacity / 4) return 0;
   slot = hash & (cache->capacity - 1);
   while (cache->slots[slot].epoch == cache->epoch) slot = (slot + 1) & (cache->capacity - 1);
   cache->slots[slot].record = record;
   cache->slots[slot].hash = hash;
   cache->slots[slot].epoch = cache->epoch;
   ++cache->count;
   return 1;
}
#endif
