/* Exclusive compiler registry. Groups retain contiguous pointer storage across
 * leases, borrowing records whose identity/mutation rules belong to the caller. */
#ifndef XE_CORE_SPV_GROUPS_H
#define XE_CORE_SPV_GROUPS_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <retro_inline.h>
typedef struct xe_spv_record_view {
   void *const *records;
   size_t count;
} xe_spv_record_view;
typedef struct xe_spv_group {
   void **records;
   size_t count, capacity;
   uint32_t key, epoch;
} xe_spv_group;
typedef struct xe_spv_groups {
   xe_spv_group *slots;
   size_t count, capacity;
   uint32_t epoch;
} xe_spv_groups;
static INLINE uint32_t xe_spv_group_key(uint32_t opcode, int debug)
{
   /* Both SPIR-V and nonsemantic opcodes fit 16 bits. */
   return (opcode << 1) | (debug != 0);
}
static INLINE size_t xe_spv_group_slot(uint32_t key, size_t capacity)
{
   uint32_t hash = key * UINT32_C(0x9e3779b9);
   hash ^= hash >> 16;
   return hash & (capacity - 1);
}
static INLINE xe_spv_group *xe_spv_groups_find(const xe_spv_groups *groups, uint32_t key)
{
   size_t slot, remaining;
   if (!groups->capacity) return NULL;
   slot = xe_spv_group_slot(key, groups->capacity);
   for (remaining = groups->capacity; remaining; --remaining) {
      xe_spv_group *entry = groups->slots + slot;
      if (!entry->epoch) return NULL;
      if (entry->key == key) return entry;
      slot = (slot + 1) & (groups->capacity - 1);
   }
   return NULL;
}
static INLINE xe_spv_record_view xe_spv_groups_view(const xe_spv_groups *groups, uint32_t key)
{
   xe_spv_record_view view;
   const xe_spv_group *entry = xe_spv_groups_find(groups, key);
   view.records = NULL;
   view.count = 0;
   if (entry && entry->epoch == groups->epoch) {
      view.records = entry->records;
      view.count = entry->count;
   }
   return view;
}
static INLINE void xe_spv_groups_reset(xe_spv_groups *groups)
{
   size_t i;
   ++groups->epoch;
   if (!groups->epoch) {
      /* Keep occupied keys/buffers; mark all as belonging to another lease. */
      groups->epoch = 1;
      for (i = 0; i < groups->capacity; ++i)
         if (groups->slots[i].epoch) groups->slots[i].epoch = 2;
   }
}
static INLINE int xe_spv_groups_reserve(xe_spv_groups *groups, size_t count,
      void *(*allocate)(size_t), void (*release)(void *))
{
   xe_spv_group *slots;
   size_t capacity = groups->capacity ? groups->capacity : 16;
   size_t limit = (size_t)-1 / sizeof(*slots), i, slot;
   if (!groups->epoch) xe_spv_groups_reset(groups);
   if (count <= groups->capacity - groups->capacity / 4) return 1;
   while (count > capacity - capacity / 4) {
      if (capacity > limit / 2) return 0;
      capacity *= 2;
   }
   if (capacity > limit) return 0;
   slots = (xe_spv_group *)allocate(capacity * sizeof(*slots));
   if (!slots) return 0;
   memset(slots, 0, capacity * sizeof(*slots));
   for (i = 0; i < groups->capacity; ++i) {
      const xe_spv_group *old = groups->slots + i;
      if (!old->epoch) continue;
      slot = xe_spv_group_slot(old->key, capacity);
      while (slots[slot].epoch) slot = (slot + 1) & (capacity - 1);
      slots[slot] = *old;
   }
   release(groups->slots);
   groups->slots = slots;
   groups->capacity = capacity;
   return 1;
}
static INLINE xe_spv_group *xe_spv_groups_get(xe_spv_groups *groups, uint32_t key,
      void *(*allocate)(size_t), void (*release)(void *))
{
   xe_spv_group *entry = xe_spv_groups_find(groups, key);
   size_t slot;
   if (!entry) {
      if (groups->count == (size_t)-1 ||
          !xe_spv_groups_reserve(groups, groups->count + 1, allocate, release)) return NULL;
      slot = xe_spv_group_slot(key, groups->capacity);
      while (groups->slots[slot].epoch) slot = (slot + 1) & (groups->capacity - 1);
      entry = groups->slots + slot;
      entry->key = key;
      entry->epoch = groups->epoch;
      ++groups->count;
   }
   if (entry->epoch != groups->epoch) {
      entry->count = 0;
      entry->epoch = groups->epoch;
   }
   return entry;
}
static INLINE int xe_spv_group_reserve(xe_spv_group *group, size_t count,
      void *(*allocate)(size_t), void (*release)(void *))
{
   void **records;
   size_t capacity = group->capacity ? group->capacity : 4;
   size_t limit = (size_t)-1 / sizeof(*records);
   if (count <= group->capacity) return 1;
   while (capacity < count) {
      if (capacity > limit / 2) { capacity = count; break; }
      capacity *= 2;
   }
   if (capacity > limit) return 0;
   records = (void **)allocate(capacity * sizeof(*records));
   if (!records) return 0;
   if (group->count) memcpy(records, group->records, group->count * sizeof(*records));
   release(group->records);
   group->records = records;
   group->capacity = capacity;
   return 1;
}
static INLINE int xe_spv_groups_append(xe_spv_groups *groups, uint32_t key, void *record,
      void *(*allocate)(size_t), void (*release)(void *))
{
   xe_spv_group *group;
   if (!record) return 0;
   group = xe_spv_groups_get(groups, key, allocate, release);
   if (!group || group->count == (size_t)-1 ||
       !xe_spv_group_reserve(group, group->count + 1, allocate, release)) return 0;
   group->records[group->count++] = record;
   return 1;
}
static INLINE void xe_spv_groups_close(xe_spv_groups *groups, void (*release)(void *))
{
   size_t i;
   for (i = 0; i < groups->capacity; ++i) release(groups->slots[i].records);
   release(groups->slots);
   memset(groups, 0, sizeof(*groups));
}
#endif
