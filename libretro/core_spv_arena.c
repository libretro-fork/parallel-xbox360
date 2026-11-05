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
enum { PAGE_BYTES = XE_SPV_RECORD_BYTES * XE_SPV_RECORDS_PER_PAGE };
typedef struct xe_spv_arena_page {
   struct xe_spv_arena_page *next;
   unsigned char *records;
   size_t used;
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
   return 1;
}

int xe_spv_arena_begin(xe_spv_arena *arena)
{
   xe_spv_arena_page *page;
   if (arena->in_use) return 0;
   for (page = arena->head; page; page = page->next) page->used = 0;
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

int xe_spv_arena_end(xe_spv_arena *arena, void (*destroy)(void *record))
{
   xe_spv_arena_page *page;
   size_t i, slot;
   uint32_t mask;
   if (!arena->in_use) return 0;
   for (page = arena->head; page; page = page->next) {
      /* Normal typed owners have already retired everything: skip 32 records
       * at a time, visiting only orphan bits during exception cleanup. */
      for (i = 0; i < XE_SPV_RECORDS_PER_PAGE / 32; ++i) {
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
   arena->in_use = 0;
   arena->cursor = NULL;
   return 1;
}

int xe_spv_arena_close(xe_spv_arena *arena)
{
   xe_spv_arena_page *page, *next;
   if (arena->in_use) return 0;
   for (page = arena->head; page; page = next) {
      next = page->next;
      UNPOISON(page->records, PAGE_BYTES);
      XE_SPV_ARENA_FREE(page);
   }
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

size_t xe_spv_arena_used(const xe_spv_arena *arena)
{
   const xe_spv_arena_page *page;
   size_t used = 0;
   for (page = arena->head; page; page = page->next) used += page->used;
   return used;
}
