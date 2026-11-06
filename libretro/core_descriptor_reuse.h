/* Single command-processor owner. Records own CPU metadata, never GPU handles.
 * One lease at a time; commit only after obtaining a valid native handle. */
#ifndef XE_CORE_DESCRIPTOR_REUSE_H
#define XE_CORE_DESCRIPTOR_REUSE_H
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <retro_inline.h>
#ifndef XE_DESCRIPTOR_REUSE_ALLOC
#define XE_DESCRIPTOR_REUSE_ALLOC malloc
#define XE_DESCRIPTOR_REUSE_FREE free
#endif
enum { XE_DESCRIPTOR_REUSE_PAGE_SLOTS = 128 };
typedef struct xe_descriptor_reuse_node {
  struct xe_descriptor_reuse_node *next;
  uint64_t frame, handle;
} xe_descriptor_reuse_node;
typedef struct xe_descriptor_reuse_page {
  struct xe_descriptor_reuse_page *next;
  xe_descriptor_reuse_node nodes[XE_DESCRIPTOR_REUSE_PAGE_SLOTS];
} xe_descriptor_reuse_page;
typedef struct xe_descriptor_reuse {
  xe_descriptor_reuse_page *pages, *last_page, *cursor;
  size_t next_slot;
  xe_descriptor_reuse_node *free, *head, *tail, *leased;
} xe_descriptor_reuse;
static INLINE int xe_descriptor_reuse_warm(xe_descriptor_reuse *pool) {
  xe_descriptor_reuse_page *page;
  if (pool->pages) return 1;
  page = (xe_descriptor_reuse_page *)XE_DESCRIPTOR_REUSE_ALLOC(sizeof(*page));
  if (!page) return 0;
  page->next = NULL; pool->pages = pool->last_page = pool->cursor = page;
  pool->next_slot = 0; return 1;
}
static INLINE xe_descriptor_reuse_node *xe_descriptor_reuse_acquire(xe_descriptor_reuse *pool, uint64_t frame) {
  xe_descriptor_reuse_node *node;
  xe_descriptor_reuse_page *page;
  size_t offset;
  if (pool->leased || (pool->tail && frame < pool->tail->frame)) return NULL;
  if (pool->free) {
    node = pool->free; pool->free = node->next;
  } else {
    if (!xe_descriptor_reuse_warm(pool)) return NULL;
    page = pool->cursor; offset = pool->next_slot;
    if (offset == XE_DESCRIPTOR_REUSE_PAGE_SLOTS) { page = page->next; offset = 0; }
    if (!page) {
      page = (xe_descriptor_reuse_page *)XE_DESCRIPTOR_REUSE_ALLOC(sizeof(*page));
      if (!page) return NULL;
      page->next = NULL; pool->last_page->next = page; pool->last_page = page;
    }
    pool->cursor = page; pool->next_slot = offset + 1;
    node = page->nodes + offset; node->handle = 0;
  }
  node->frame = frame; node->next = NULL; pool->leased = node; return node;
}
static INLINE int xe_descriptor_reuse_commit(xe_descriptor_reuse *pool) {
  xe_descriptor_reuse_node *node = pool->leased;
  if (!node || !node->handle) return 0;
  if (pool->tail) pool->tail->next = node;
  else pool->head = node;
  pool->tail = node; pool->leased = NULL; return 1;
}
static INLINE void xe_descriptor_reuse_cancel(xe_descriptor_reuse *pool) {
  xe_descriptor_reuse_node *node = pool->leased;
  if (!node) return;
  node->next = pool->free; pool->free = node; pool->leased = NULL;
}
static INLINE size_t xe_descriptor_reuse_collect(xe_descriptor_reuse *pool, uint64_t completed) {
  xe_descriptor_reuse_node *node;
  size_t count = 0;
  while ((node = pool->head) != NULL && node->frame <= completed) {
    pool->head = node->next; node->next = pool->free; pool->free = node; ++count;
  }
  if (!pool->head) pool->tail = NULL;
  return count;
}
/* At the existing native-pool reset boundary only. Keep pages, forget handles
 * lazily when leasing retained virgin slots. No capacity-sized clear/copy. */
static INLINE int xe_descriptor_reuse_reset(xe_descriptor_reuse *pool) {
  if (pool->leased) return 0;
  pool->free = pool->head = pool->tail = NULL;
  pool->cursor = pool->pages; pool->next_slot = 0; return 1;
}
static INLINE void xe_descriptor_reuse_destroy(xe_descriptor_reuse *pool) {
  xe_descriptor_reuse_page *page = pool->pages, *next;
  while (page) { next = page->next; XE_DESCRIPTOR_REUSE_FREE(page); page = next; }
  pool->pages = pool->last_page = pool->cursor = NULL; pool->next_slot = 0;
  pool->free = pool->head = pool->tail = pool->leased = NULL;
}
#ifdef __cplusplus
/* Thin exception boundary for existing C++ native allocators. */
struct xe_descriptor_reuse_scope {
  xe_descriptor_reuse *pool;
  explicit xe_descriptor_reuse_scope(xe_descriptor_reuse *p) : pool(p) {}
  xe_descriptor_reuse_scope(const xe_descriptor_reuse_scope&) = delete;
  xe_descriptor_reuse_scope& operator=(const xe_descriptor_reuse_scope&) = delete;
  ~xe_descriptor_reuse_scope() { xe_descriptor_reuse_cancel(pool); }
};
#endif
#endif
