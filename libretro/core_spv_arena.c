#include "core_spv_arena.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <compat/intrinsics.h>
#ifdef XE_SPV_ARENA_ASAN
#include <sanitizer/asan_interface.h>
#define POISON(p, n) __asan_poison_memory_region((p), (n))
#define UNPOISON(p, n) __asan_unpoison_memory_region((p), (n))
#else
#define POISON(p, n) ((void)0)
#define UNPOISON(p, n) ((void)0)
#endif
#ifndef XE_SPV_ARENA_ALLOC
#define XE_SPV_ARENA_ALLOC malloc
#define XE_SPV_ARENA_FREE free
#endif
/* Object headers stay outside C++ payload lifetimes and ASan-poisoned ranges. */
typedef struct object_page {
   struct object_page *next;
   unsigned char *records;
   size_t used, bytes, stride, capacity;
   uint32_t live[2];
} object_page;
typedef union object_tag {
   struct { object_page *page; uint32_t mask, group; } value;
   unsigned char alignment[16];
} object_tag;
typedef struct object_pool {
   object_page *head, *tail, *cursor;
   size_t bytes, reserved;
} object_pool;
typedef struct payload_page {
   struct payload_page *next;
   unsigned char *data;
   size_t capacity, used;
} payload_page;
typedef struct payload_pool {
   payload_page *head, *tail, *cursor, *last_page;
   void *last;
   size_t last_bytes, last_stride;
} payload_pool;
typedef struct xe_spv_object_state {
   object_pool pool[2];
   payload_pool payloads;
   xe_spv_constant_cache constants;
   xe_spv_groups groups;
} xe_spv_object_state;

enum { PAYLOAD_PAGE_BYTES = 16384 };
#ifdef XE_SPV_ARENA_ASAN
#define PAYLOAD_REDZONE 8
#else
#define PAYLOAD_REDZONE 0
#endif
typedef char object_tag_size_must_be_16[sizeof(object_tag) == 16 ? 1 : -1];

static xe_spv_object_state *object_state(xe_spv_arena *arena)
{
   if (!arena->objects) {
      arena->objects = (xe_spv_object_state *)XE_SPV_ARENA_ALLOC(sizeof(*arena->objects));
      if (arena->objects) memset(arena->objects, 0, sizeof(*arena->objects));
   }
   return arena->objects;
}
xe_spv_constant_cache *xe_spv_arena_constants(xe_spv_arena *arena, size_t extra)
{
   xe_spv_object_state *state;
   xe_spv_constant_cache *cache;
   if (!arena->in_use || !(state = object_state(arena))) return NULL;
   cache = &state->constants;
   if (!cache->epoch) xe_spv_constant_cache_reset(cache);
   if (extra > (size_t)-1 - cache->count ||
       !xe_spv_constant_cache_reserve(cache, cache->count + extra,
          XE_SPV_ARENA_ALLOC, XE_SPV_ARENA_FREE)) return NULL;
   return cache;
}
int xe_spv_arena_reserve_constants(xe_spv_arena *arena, size_t count)
{
   xe_spv_object_state *state = object_state(arena);
   if (!state) return 0;
   if (!state->constants.epoch) xe_spv_constant_cache_reset(&state->constants);
   return xe_spv_constant_cache_reserve(&state->constants, count,
         XE_SPV_ARENA_ALLOC, XE_SPV_ARENA_FREE);
}
xe_spv_record_view xe_spv_arena_group_view(const xe_spv_arena *arena, uint32_t key)
{
   xe_spv_record_view empty = {NULL, 0};
   if (!arena->in_use || !arena->objects) return empty;
   return xe_spv_groups_view(&arena->objects->groups, key);
}
int xe_spv_arena_group_append(xe_spv_arena *arena, uint32_t key, void *record)
{
   xe_spv_object_state *state;
   if (!arena->in_use || !(state = object_state(arena))) return 0;
   return xe_spv_groups_append(&state->groups, key, record, XE_SPV_ARENA_ALLOC, XE_SPV_ARENA_FREE);
}
int xe_spv_arena_reserve_type_groups(xe_spv_arena *arena)
{
   /* Stable core SPIR-V type opcodes: Void through Struct, Pointer, Function.
    * Extended/debug groups grow on demand; no fixed group-count restriction. */
   static const uint32_t opcodes[] = {19,20,21,22,23,24,25,26,27,28,29,30,32,33};
   xe_spv_object_state *state = object_state(arena);
   size_t i;
   if (!state || !xe_spv_groups_reserve(&state->groups, 32, XE_SPV_ARENA_ALLOC, XE_SPV_ARENA_FREE)) return 0;
   for (i = 0; i < sizeof(opcodes) / sizeof(opcodes[0]); ++i) {
      xe_spv_group *group = xe_spv_groups_get(&state->groups, xe_spv_group_key(opcodes[i], 0),
            XE_SPV_ARENA_ALLOC, XE_SPV_ARENA_FREE);
      if (!group || !xe_spv_group_reserve(group, 4, XE_SPV_ARENA_ALLOC, XE_SPV_ARENA_FREE)) return 0;
   }
   return 1;
}
static payload_page *payload_grow(payload_pool *pool, size_t minimum)
{
   payload_page *page;
   uintptr_t address;
   size_t capacity = minimum > PAYLOAD_PAGE_BYTES ? minimum : PAYLOAD_PAGE_BYTES;
   if (capacity > (size_t)-1 - sizeof(*page) - 7) return NULL;
   page = (payload_page *)XE_SPV_ARENA_ALLOC(sizeof(*page) + 7 + capacity);
   if (!page) return NULL;
   address = ((uintptr_t)(page + 1) + 7) & ~(uintptr_t)7;
   page->data = (unsigned char *)address;
   page->capacity = capacity;
   page->used = 0;
   page->next = NULL;
   /* Cold prefault only; warm leases reset metadata, never payload bytes. */
   memset(page->data, 0, capacity);
   POISON(page->data, capacity);
   if (pool->tail) pool->tail->next = page;
   else pool->head = page;
   pool->tail = page;
   return page;
}
int xe_spv_arena_reserve_payloads(xe_spv_arena *arena, size_t bytes)
{
   payload_pool *pool;
   payload_page *page;
   size_t capacity = 0;
   if (arena->in_use || bytes > (size_t)-1 - sizeof(payload_page) - 7) return 0;
   if (!bytes) return 1;
   if (!object_state(arena)) return 0;
   pool = &arena->objects->payloads;
   for (page = pool->head; page; page = page->next) {
      if (page->capacity > (size_t)-1 - capacity) return 0;
      capacity += page->capacity;
   }
   return capacity >= bytes || payload_grow(pool, bytes - capacity) != NULL;
}
void *xe_spv_arena_payload_realloc(void *context, void *old, size_t old_bytes, size_t bytes)
{
   xe_spv_arena *arena = (xe_spv_arena *)context;
   payload_pool *pool;
   payload_page *page;
   void *result;
   size_t padded, stride, before;
   if (!arena || !arena->in_use || !bytes || bytes > (size_t)-1 - 7 - PAYLOAD_REDZONE ||
       !object_state(arena)) return NULL;
   padded = (bytes + 7) & ~(size_t)7;
   stride = padded + PAYLOAD_REDZONE;
   pool = &arena->objects->payloads;
   page = pool->last_page;
   if (old && old == pool->last && old_bytes == pool->last_bytes && page &&
       bytes >= old_bytes && page->used >= pool->last_stride) {
      before = page->used - pool->last_stride;
      if (stride <= page->capacity - before) {
         page->used = before + stride;
         pool->last_bytes = bytes;
         pool->last_stride = stride;
         UNPOISON(old, padded);
         return old;
      }
   }
   page = pool->cursor ? pool->cursor : pool->head;
   while (page && stride > page->capacity - page->used) page = page->next;
   if (!page) page = payload_grow(pool, stride);
   if (!page) return NULL;
   result = page->data + page->used;
   page->used += stride;
   pool->cursor = pool->last_page = page;
   pool->last = result;
   pool->last_bytes = bytes;
   pool->last_stride = stride;
   UNPOISON(result, padded);
   return result;
}
static void payloads_end(xe_spv_arena *arena)
{
   payload_pool *pool;
   payload_page *page;
   if (!arena->objects) return;
   pool = &arena->objects->payloads;
   for (page = pool->head; page; page = page->next) {
      POISON(page->data, page->capacity);
      page->used = 0;
   }
   pool->cursor = pool->head;
   pool->last_page = NULL;
   pool->last = NULL;
   pool->last_bytes = pool->last_stride = 0;
}
static object_page *object_grow(object_pool *pool, unsigned kind)
{
   object_page *page;
   size_t stride, capacity = kind == XE_SPV_OBJECT_BLOCK ? 64 : 32;
   size_t i;
   uintptr_t address;
   if (!pool->bytes || pool->bytes > (size_t)-1 - sizeof(object_tag) - 15) return NULL;
   stride = (pool->bytes + sizeof(object_tag) + 15) & ~(size_t)15;
   if (stride > ((size_t)-1 - sizeof(*page) - 15) / capacity ||
       pool->reserved > (size_t)-1 - capacity) return NULL;
   page = (object_page *)XE_SPV_ARENA_ALLOC(sizeof(*page) + 15 + stride * capacity);
   if (!page) return NULL;
   address = ((uintptr_t)(page + 1) + 15) & ~(uintptr_t)15;
   page->records = (unsigned char *)address + sizeof(object_tag);
   page->bytes = pool->bytes;
   page->stride = stride;
   page->capacity = capacity;
   page->used = 0;
   page->next = NULL;
   memset(page->live, 0, sizeof(page->live));
   /* Cold prefault only; allocation/reset never copies or clears object payload. */
   memset((void *)address, 0, stride * capacity);
   for (i = 0; i < capacity; ++i) POISON(page->records + i * stride, page->bytes);
   if (pool->tail) pool->tail->next = page;
   else pool->head = page;
   pool->tail = page;
   pool->reserved += capacity;
   return page;
}
int xe_spv_arena_reserve_objects(xe_spv_arena *arena, size_t block_bytes,
      size_t blocks, size_t function_bytes, size_t functions)
{
   object_pool *pool;
   size_t bytes, count;
   unsigned kind;
   if (arena->in_use || !block_bytes || !function_bytes || !object_state(arena)) return 0;
   for (kind = 0; kind < 2; ++kind) {
      pool = &arena->objects->pool[kind];
      bytes = kind == XE_SPV_OBJECT_BLOCK ? block_bytes : function_bytes;
      count = kind == XE_SPV_OBJECT_BLOCK ? blocks : functions;
      if ((pool->bytes && pool->bytes != bytes) || count > (size_t)-1 / bytes) return 0;
      pool->bytes = bytes;
      while (pool->reserved < count) if (!object_grow(pool, kind)) return 0;
   }
   return 1;
}
static int objects_begin(xe_spv_arena *arena)
{
   unsigned kind;
   object_pool *pool;
   object_page *page;
   if (!arena->objects) return 1;
   for (kind = 0; kind < 2; ++kind) {
      pool = &arena->objects->pool[kind];
      for (page = pool->head; page; page = page->next)
         if (page->live[0] || page->live[1]) return 0;
   }
   for (kind = 0; kind < 2; ++kind) {
      pool = &arena->objects->pool[kind];
      for (page = pool->head; page; page = page->next) page->used = 0;
      pool->cursor = pool->head;
   }
   return 1;
}
void *xe_spv_arena_object_alloc(xe_spv_arena *arena, unsigned kind, size_t bytes)
{
   object_pool *pool;
   object_page *page;
   object_tag *tag;
   void *record;
   if (!arena->in_use || kind > XE_SPV_OBJECT_FUNCTION || !bytes || !object_state(arena)) return NULL;
   pool = &arena->objects->pool[kind];
   if (pool->bytes && pool->bytes != bytes) return NULL;
   pool->bytes = bytes;
   page = pool->cursor;
   if (page && page->used == page->capacity) page = page->next;
   if (!page) page = object_grow(pool, kind);
   if (!page) return NULL;
   pool->cursor = page;
   record = page->records + page->used * page->stride;
   UNPOISON(record, page->bytes);
   tag = (object_tag *)((unsigned char *)record - sizeof(*tag));
   tag->value.page = page;
   tag->value.group = (uint32_t)(page->used / 32);
   tag->value.mask = UINT32_C(1) << (page->used % 32);
   page->live[tag->value.group] |= tag->value.mask;
   ++page->used;
   return record;
}
void xe_spv_arena_object_retire(void *record)
{
   object_tag *tag;
   object_page *page;
   if (!record) return;
   tag = (object_tag *)((unsigned char *)record - sizeof(*tag));
   page = tag->value.page;
   assert(tag->value.group < 2 && (page->live[tag->value.group] & tag->value.mask));
   page->live[tag->value.group] &= ~tag->value.mask;
   POISON(record, page->bytes);
}
int xe_spv_arena_objects_end(xe_spv_arena *arena,
      void (*destroy_function)(void *), void (*destroy_block)(void *))
{
   unsigned kind, group, slot;
   object_page *page;
   uint32_t mask;
   void (*destroy)(void *);
   if (!arena->in_use) return 0;
   if (!arena->objects) return 1;
   /* Orphan functions can own blocks. Block payloads can own instructions. */
   for (kind = 2; kind-- > 0;) {
      destroy = kind == XE_SPV_OBJECT_FUNCTION ? destroy_function : destroy_block;
      for (page = arena->objects->pool[kind].head; page; page = page->next) {
         for (group = 0; group < 2; ++group) {
            while (page->live[group]) {
               if (!destroy) return 0;
               slot = 31 - compat_clz_u32(page->live[group]);
               mask = UINT32_C(1) << slot;
               destroy(page->records + (group * 32 + slot) * page->stride);
               if (page->live[group] & mask) return 0;
            }
         }
      }
   }
   return 1;
}
static int objects_empty(const xe_spv_arena *arena)
{
   unsigned kind;
   const object_page *page;
   if (!arena->objects) return 1;
   for (kind = 0; kind < 2; ++kind)
      for (page = arena->objects->pool[kind].head; page; page = page->next)
         if (page->live[0] || page->live[1]) return 0;
   return 1;
}
static void objects_close(xe_spv_arena *arena)
{
   unsigned kind;
   object_page *page, *next;
   payload_page *payload, *payload_next;
   if (!arena->objects) return;
   for (kind = 0; kind < 2; ++kind) {
      for (page = arena->objects->pool[kind].head; page; page = next) {
         next = page->next;
         UNPOISON(page->records - sizeof(object_tag), page->stride * page->capacity);
         XE_SPV_ARENA_FREE(page);
      }
   }
   for (payload = arena->objects->payloads.head; payload; payload = payload_next) {
      payload_next = payload->next;
      UNPOISON(payload->data, payload->capacity);
      XE_SPV_ARENA_FREE(payload);
   }
   XE_SPV_ARENA_FREE(arena->objects->constants.slots);
   xe_spv_groups_close(&arena->objects->groups, XE_SPV_ARENA_FREE);
   XE_SPV_ARENA_FREE(arena->objects);
   arena->objects = NULL;
}
enum { PAGE_BYTES = XE_SPV_RECORD_BYTES * XE_SPV_RECORDS_PER_PAGE };
typedef struct xe_spv_arena_page {
   struct xe_spv_arena_page *next;
   unsigned char *records;
   size_t used;
   xe_spv_arena *owner;
   uint32_t live[XE_SPV_RECORDS_PER_PAGE / 32];
} xe_spv_arena_page;

static xe_spv_arena_page *grow(xe_spv_arena *arena)
{
   xe_spv_arena_page *page;
   uintptr_t address;
   if (arena->reserved_records > (size_t)-1 / XE_SPV_RECORD_BYTES - XE_SPV_RECORDS_PER_PAGE)
      return NULL;
   page = (xe_spv_arena_page *)XE_SPV_ARENA_ALLOC(sizeof(*page) + PAGE_BYTES + 63);
   if (!page) return NULL;
   address = ((uintptr_t)(page + 1) + 63) & ~(uintptr_t)63;
   page->records = (unsigned char *)address;
   page->owner = arena;
   page->next = NULL;
   page->used = 0;
   memset(page->live, 0, sizeof(page->live));
   /* Cold prefault; reuse never clears or copies record payload. */
   memset(page->records, 0, PAGE_BYTES);
   POISON(page->records, PAGE_BYTES);
   if (arena->tail) arena->tail->next = page;
   else arena->head = page;
   arena->tail = page;
   arena->reserved_records += XE_SPV_RECORDS_PER_PAGE;
   return page;
}

int xe_spv_arena_reserve(xe_spv_arena *arena, size_t records)
{
   if (arena->in_use || records > (size_t)-1 / XE_SPV_RECORD_BYTES) return 0;
   while (arena->reserved_records < records)
      if (!grow(arena)) return 0;
   return xe_spv_index_reserve(&arena->index, records);
}

int xe_spv_arena_begin(xe_spv_arena *arena)
{
   xe_spv_arena_page *page;
   if (arena->in_use || !objects_begin(arena)) return 0;
   for (page = arena->head; page; page = page->next) page->used = 0;
   xe_spv_index_reset(&arena->index);
   arena->cursor = arena->head;
   arena->in_use = 1;
   return 1;
}

void *xe_spv_arena_alloc(xe_spv_arena *arena, uint32_t *arena_offset)
{
   xe_spv_arena_page *page;
   void *record;
   uint32_t offset;
   if (!arena->in_use || !arena_offset) return NULL;
   page = arena->cursor;
   if (page && page->used == XE_SPV_RECORDS_PER_PAGE) page = page->next;
   if (!page) page = grow(arena);
   if (!page) return NULL;
   arena->cursor = page;
   record = page->records + page->used * XE_SPV_RECORD_BYTES;
   UNPOISON(record, XE_SPV_RECORD_BYTES);
   offset = (uint32_t)((unsigned char *)record - (unsigned char *)page);
   *arena_offset = offset;
   memcpy((unsigned char *)record + XE_SPV_RECORD_ARENA_OFFSET, &offset, sizeof(offset));
   page->live[page->used / 32] |= UINT32_C(1) << (page->used % 32);
   ++page->used;
   return record;
}

static int arena_end(xe_spv_arena *arena, void (*destroy)(void *record), int payload_only)
{
   xe_spv_arena_page *page;
   size_t i, slot;
   uint32_t mask;
   if (!arena->in_use || !objects_empty(arena)) return 0;
   for (page = arena->head; page; page = page->next) {
      /* Skip empty groups. Borrowed global lists need no separate ownership
       * walk: payload-only cleanup snapshots each live bitmap once. */
      for (i = 0; i < XE_SPV_RECORDS_PER_PAGE / 32; ++i) {
         if (payload_only) {
            mask = page->live[i];
            if (mask && !destroy) return 0;
            while (mask) {
               slot = 31 - compat_clz_u32(mask);
               destroy(page->records + (i * 32 + slot) * XE_SPV_RECORD_BYTES);
               mask &= ~(UINT32_C(1) << slot);
            }
            page->live[i] = 0;
            continue;
         }
         while (page->live[i]) {
            if (!destroy) return 0;
            slot = 31 - compat_clz_u32(page->live[i]);
            mask = UINT32_C(1) << slot;
            destroy(page->records + (i * 32 + slot) * XE_SPV_RECORD_BYTES);
            if (page->live[i] & mask) return 0;
         }
      }
      POISON(page->records, PAGE_BYTES);
   }
   xe_spv_index_reset(&arena->index);
   payloads_end(arena);
   if (arena->objects) xe_spv_constant_cache_reset(&arena->objects->constants);
   if (arena->objects) xe_spv_groups_reset(&arena->objects->groups);
   arena->in_use = 0;
   arena->cursor = NULL;
   return 1;
}

int xe_spv_arena_end(xe_spv_arena *arena, void (*destroy)(void *record))
{
   return arena_end(arena, destroy, 0);
}
int xe_spv_arena_finish(xe_spv_arena *arena, void (*destroy_payload)(void *record))
{
   return arena_end(arena, destroy_payload, 1);
}
int xe_spv_arena_close(xe_spv_arena *arena)
{
   xe_spv_arena_page *page, *next;
   if (arena->in_use || !objects_empty(arena)) return 0;
   objects_close(arena);
   for (page = arena->head; page; page = next) {
      next = page->next;
      UNPOISON(page->records, PAGE_BYTES);
      XE_SPV_ARENA_FREE(page);
   }
   xe_spv_index_close(&arena->index);
   memset(arena, 0, sizeof(*arena));
   return 1;
}
void xe_spv_arena_retire(void *record)
{
   uint32_t offset;
   xe_spv_arena_page *page;
   size_t slot;
   /* Access storage bytes, not members of the already-destroyed C++ object. */
   memcpy(&offset, (unsigned char *)record + XE_SPV_RECORD_ARENA_OFFSET, sizeof(offset));
   assert(offset >= sizeof(*page) && offset < sizeof(*page) + 63 + PAGE_BYTES);
   page = (xe_spv_arena_page *)((unsigned char *)record - offset);
   slot = (size_t)((unsigned char *)record - page->records) / XE_SPV_RECORD_BYTES;
   assert(slot < page->used && (page->live[slot / 32] & (UINT32_C(1) << (slot % 32))));
   page->live[slot / 32] &= ~(UINT32_C(1) << (slot % 32));
   POISON(record, XE_SPV_RECORD_BYTES);
}
void *xe_spv_arena_record_owner(const void *record, uint32_t offset)
{
   const xe_spv_arena_page *page;
   if (!record || !offset) return NULL;
   assert(offset >= sizeof(*page) && offset < sizeof(*page) + 63 + PAGE_BYTES);
   page = (const xe_spv_arena_page *)((const unsigned char *)record - offset);
   assert(page->owner->in_use);
   return page->owner;
}

int xe_spv_arena_owns_record(const xe_spv_arena *arena, const void *record)
{
   const xe_spv_arena_page *page;
   uintptr_t address = (uintptr_t)record, start;
   size_t slot;
   if (!arena->in_use || !record) return 0;
   for (page = arena->head; page; page = page->next) {
      start = (uintptr_t)page->records;
      if (address >= start && address - start < page->used * XE_SPV_RECORD_BYTES &&
          !((address - start) % XE_SPV_RECORD_BYTES)) {
         slot = (size_t)(address - start) / XE_SPV_RECORD_BYTES;
         return (page->live[slot / 32] & (UINT32_C(1) << (slot % 32))) != 0;
      }
   }
   return 0;
}
size_t xe_spv_arena_used(const xe_spv_arena *arena)
{
   const xe_spv_arena_page *page;
   size_t used = 0;
   for (page = arena->head; page; page = page->next) used += page->used;
   return used;
}
