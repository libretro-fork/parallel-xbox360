/* Native descriptor pools have one owner independent of allocator search maps.
 * Reserve CPU ownership before creating a pool. No allocation when committing.
 * Reset requires the caller's existing GPU-completion / context-stop boundary. */
#ifndef XE_CORE_VK_POOL_OWNER_H
#define XE_CORE_VK_POOL_OWNER_H
#include <stddef.h>
#include <stdlib.h>
#include <vulkan/vulkan_core.h>
#include <retro_inline.h>
#ifndef XE_POOL_OWNER_ALLOC
#define XE_POOL_OWNER_ALLOC malloc
#define XE_POOL_OWNER_FREE free
#endif
enum { XE_POOL_OWNER_SLOTS = 64 };
typedef struct xe_vk_pool_record { VkDescriptorPool pool; } xe_vk_pool_record;
typedef struct xe_vk_pool_page {
  struct xe_vk_pool_page *next;
  xe_vk_pool_record records[XE_POOL_OWNER_SLOTS];
} xe_vk_pool_page;
typedef struct xe_vk_pool_owner {
  xe_vk_pool_page *pages, *last, *cursor;
  size_t next_slot, count, type_count, page_bytes;
  xe_vk_pool_record *leased;
} xe_vk_pool_owner;
/* Optional trailing counters are configured once, before warming any pages.
 * No fixed descriptor-type cap: checked size arithmetic defines capacity. */
static INLINE int xe_vk_pool_owner_configure(xe_vk_pool_owner *s, size_t types) {
  if (s->pages || s->leased || s->count) return 0;
  if (types > (((size_t)-1 - sizeof(xe_vk_pool_page)) /
      sizeof(VkDescriptorPoolSize)) / XE_POOL_OWNER_SLOTS) return 0;
  s->type_count = types;
  s->page_bytes = sizeof(xe_vk_pool_page) + types * XE_POOL_OWNER_SLOTS * sizeof(VkDescriptorPoolSize);
  return 1;
}
static INLINE xe_vk_pool_record *xe_vk_pool_owner_acquire(xe_vk_pool_owner *s) {
  xe_vk_pool_page *page;
  xe_vk_pool_record *record;
  size_t slot;
  if (s->leased || s->count == (size_t)-1) return NULL;
  page = s->cursor; slot = s->next_slot;
  if (page && slot == XE_POOL_OWNER_SLOTS) { page = page->next; slot = 0; }
  if (!page) {
    page = (xe_vk_pool_page *)XE_POOL_OWNER_ALLOC(s->page_bytes ? s->page_bytes : sizeof(*page));
    if (!page) return NULL;
    page->next = NULL;
    if (s->last) s->last->next = page;
    else s->pages = page;
    s->last = page;
  }
  record = page->records + slot;
  s->cursor = page; s->next_slot = slot;
  record->pool = VK_NULL_HANDLE; s->leased = record; return record;
}
/* The leased slot's counter view never moves. The owner retains its page.
 * Caller initializes every counter before first use after acquire/reset. */
static INLINE VkDescriptorPoolSize *xe_vk_pool_owner_counts(xe_vk_pool_owner *s) {
  if (!s->leased || !s->type_count) return NULL;
  return (VkDescriptorPoolSize *)(s->cursor + 1) + s->next_slot * s->type_count;
}
static INLINE int xe_vk_pool_owner_commit(xe_vk_pool_owner *s) {
  if (!s->leased || s->leased->pool == VK_NULL_HANDLE) return 0;
  ++s->next_slot; ++s->count; s->leased = NULL; return 1;
}
/* Caller destroys any uncommitted native pool first. A canceled slot is reused. */
static INLINE void xe_vk_pool_owner_cancel(xe_vk_pool_owner *s) {
  if (!s->leased) return;
  s->leased->pool = VK_NULL_HANDLE; s->leased = NULL;
}
static INLINE int xe_vk_pool_owner_warm(xe_vk_pool_owner *s) {
  if (!xe_vk_pool_owner_acquire(s)) return 0;
  xe_vk_pool_owner_cancel(s); return 1;
}
static INLINE int xe_vk_pool_owner_reset(xe_vk_pool_owner *s, VkDevice device,
    PFN_vkDestroyDescriptorPool destroy) {
  xe_vk_pool_page *page;
  size_t remaining, n, i;
  if (s->leased) return 0;
  remaining = s->count;
  for (page = s->pages; remaining; page = page->next) {
    n = remaining < XE_POOL_OWNER_SLOTS ? remaining : (size_t)XE_POOL_OWNER_SLOTS;
    for (i = 0; i < n; ++i) destroy(device, page->records[i].pool, NULL);
    remaining -= n;
  }
  s->count = 0; s->cursor = s->pages; s->next_slot = 0; return 1;
}
/* Only after reset destroyed every native pool. */
static INLINE int xe_vk_pool_owner_destroy(xe_vk_pool_owner *s) {
  xe_vk_pool_page *page = s->pages, *next;
  if (s->count || s->leased) return 0;
  while (page) { next = page->next; XE_POOL_OWNER_FREE(page); page = next; }
  s->pages = s->last = s->cursor = NULL; s->next_slot = 0; return 1;
}
#endif
