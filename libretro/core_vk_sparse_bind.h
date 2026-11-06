/* Single command-processor owner. Vulkan borrows records until QueueBindSparse
 * returns. Clear only after success, or when tearing down the command context. */
#ifndef XE_CORE_VK_SPARSE_BIND_H
#define XE_CORE_VK_SPARSE_BIND_H
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <vulkan/vulkan_core.h>
#include <retro_inline.h>
#ifndef XE_SPARSE_ALLOC
#define XE_SPARSE_ALLOC malloc
#define XE_SPARSE_REALLOC realloc
#define XE_SPARSE_FREE free
#endif
enum { XE_SPARSE_PAGE_BINDS = 128 };
typedef struct xe_sparse_bind_page {
  struct xe_sparse_bind_page *next;
  VkSparseMemoryBind binds[XE_SPARSE_PAGE_BINDS];
} xe_sparse_bind_page;
typedef struct xe_sparse_bind_queue {
  xe_sparse_bind_page *pages, *last, *cursor;
  size_t next_bind;
  VkSparseBufferMemoryBindInfo *infos;
  size_t count, capacity;
  VkPipelineStageFlags wait_stages;
} xe_sparse_bind_queue;
/* Capacity growth preserves final API records; bind pages themselves never move.
 * On failure previously queued records remain valid and submit-ready. */
static INLINE int xe_sparse_bind_reserve(xe_sparse_bind_queue *q, size_t count) {
  size_t capacity;
  VkSparseBufferMemoryBindInfo *infos;
  if (count > UINT32_MAX || count > (size_t)-1 / sizeof(*infos)) return 0;
  if (count <= q->capacity) return 1;
  capacity = q->capacity;
  if (capacity < XE_SPARSE_PAGE_BINDS) capacity = XE_SPARSE_PAGE_BINDS;
  else if (capacity <= (size_t)-1 / 2) capacity *= 2;
  if (capacity < count) capacity = count;
  if (capacity > UINT32_MAX || capacity > (size_t)-1 / sizeof(*infos)) capacity = count;
  infos = (VkSparseBufferMemoryBindInfo *)XE_SPARSE_REALLOC(q->infos, capacity * sizeof(*infos));
  if (!infos) return 0;
  q->infos = infos; q->capacity = capacity; return 1;
}
static INLINE int xe_sparse_bind_warm(xe_sparse_bind_queue *q) {
  xe_sparse_bind_page *page;
  if (!xe_sparse_bind_reserve(q, XE_SPARSE_PAGE_BINDS)) return 0;
  if (q->pages) return 1;
  page = (xe_sparse_bind_page *)XE_SPARSE_ALLOC(sizeof(*page));
  if (!page) return 0;
  page->next = NULL; q->pages = q->last = q->cursor = page;
  q->next_bind = 0; return 1;
}
static INLINE int xe_sparse_bind_push(xe_sparse_bind_queue *q, VkBuffer buffer,
    VkDeviceSize offset, VkDeviceSize size, VkDeviceMemory memory,
    VkPipelineStageFlags wait_stages) {
  xe_sparse_bind_page *page;
  VkSparseMemoryBind *bind;
  VkSparseBufferMemoryBindInfo *info;
  size_t slot;
  if (q->count == UINT32_MAX || !xe_sparse_bind_reserve(q, q->count + 1)) return 0;
  page = q->cursor; slot = q->next_bind;
  if (page && slot == XE_SPARSE_PAGE_BINDS) { page = page->next; slot = 0; }
  if (!page) {
    page = (xe_sparse_bind_page *)XE_SPARSE_ALLOC(sizeof(*page));
    if (!page) return 0;
    page->next = NULL;
    if (q->last) q->last->next = page;
    else q->pages = page;
    q->last = page;
  }
  bind = page->binds + slot;
  bind->resourceOffset = offset; bind->size = size; bind->memory = memory;
  bind->memoryOffset = 0; bind->flags = 0;
  info = q->infos + q->count;
  info->buffer = buffer; info->bindCount = 1; info->pBinds = bind;
  q->cursor = page; q->next_bind = slot + 1; ++q->count;
  q->wait_stages |= wait_stages; return 1;
}
static INLINE void xe_sparse_bind_clear(xe_sparse_bind_queue *q) {
  q->count = 0; q->wait_stages = 0; q->cursor = q->pages; q->next_bind = 0;
}
static INLINE void xe_sparse_bind_destroy(xe_sparse_bind_queue *q) {
  xe_sparse_bind_page *page = q->pages, *next;
  while (page) { next = page->next; XE_SPARSE_FREE(page); page = next; }
  XE_SPARSE_FREE(q->infos); q->infos = NULL; q->capacity = 0;
  q->pages = q->last = NULL; xe_sparse_bind_clear(q);
}
#endif
