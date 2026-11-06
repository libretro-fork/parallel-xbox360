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
enum { XE_DESCRIPTOR_REUSE_PAGE_SLOTS = 128, XE_DESCRIPTOR_REUSE_FRAME_SLOTS = 8 };
typedef struct xe_descriptor_reuse_node {
  struct xe_descriptor_reuse_node *next;
  uint64_t handle;
} xe_descriptor_reuse_node;
typedef struct xe_descriptor_reuse_page {
  struct xe_descriptor_reuse_page *next;
  xe_descriptor_reuse_node nodes[XE_DESCRIPTOR_REUSE_PAGE_SLOTS];
} xe_descriptor_reuse_page;
typedef struct xe_descriptor_reuse_frame {
  struct xe_descriptor_reuse_frame *next;
  uint64_t frame;
  xe_descriptor_reuse_node *head, *tail;
} xe_descriptor_reuse_frame;
typedef struct xe_descriptor_reuse_frame_page {
  struct xe_descriptor_reuse_frame_page *next;
  xe_descriptor_reuse_frame frames[XE_DESCRIPTOR_REUSE_FRAME_SLOTS];
} xe_descriptor_reuse_frame_page;
typedef struct xe_descriptor_reuse {
  xe_descriptor_reuse_page *pages, *last_page, *cursor;
  size_t next_slot;
  xe_descriptor_reuse_frame_page *frame_pages, *last_frame_page, *frame_cursor;
  size_t next_frame;
  xe_descriptor_reuse_node *free, *leased, *unused;
  xe_descriptor_reuse_frame *head, *tail, *free_frames, *lease_frame;
} xe_descriptor_reuse;
static INLINE int xe_descriptor_reuse_warm(xe_descriptor_reuse *pool) {
  xe_descriptor_reuse_page *page;
  xe_descriptor_reuse_frame_page *frames;
  if (!pool->pages) {
    page = (xe_descriptor_reuse_page *)XE_DESCRIPTOR_REUSE_ALLOC(sizeof(*page));
    if (!page) return 0;
    page->next = NULL; pool->pages = pool->last_page = pool->cursor = page;
    pool->next_slot = 0;
  }
  if (!pool->frame_pages) {
    frames = (xe_descriptor_reuse_frame_page *)XE_DESCRIPTOR_REUSE_ALLOC(sizeof(*frames));
    if (!frames) return 0;
    frames->next = NULL; pool->frame_pages = pool->last_frame_page = pool->frame_cursor = frames;
    pool->next_frame = 0;
  }
  return 1;
}
/* Internal: warm has initialized both page owners. */
static INLINE xe_descriptor_reuse_frame *xe_descriptor_reuse_frame_take(xe_descriptor_reuse *pool, uint64_t frame) {
  xe_descriptor_reuse_frame *batch;
  xe_descriptor_reuse_frame_page *page;
  size_t offset;
  if (pool->free_frames) {
    batch = pool->free_frames; pool->free_frames = batch->next;
  } else {
    page = pool->frame_cursor; offset = pool->next_frame;
    if (offset == XE_DESCRIPTOR_REUSE_FRAME_SLOTS) { page = page->next; offset = 0; }
    if (!page) {
      page = (xe_descriptor_reuse_frame_page *)XE_DESCRIPTOR_REUSE_ALLOC(sizeof(*page));
      if (!page) return NULL;
      page->next = NULL; pool->last_frame_page->next = page; pool->last_frame_page = page;
    }
    pool->frame_cursor = page; pool->next_frame = offset + 1;
    batch = page->frames + offset;
  }
  batch->frame = frame; batch->head = batch->tail = NULL;
  return batch;
}
static INLINE xe_descriptor_reuse_node *xe_descriptor_reuse_acquire(xe_descriptor_reuse *pool, uint64_t frame) {
  xe_descriptor_reuse_node *node;
  xe_descriptor_reuse_page *page;
  xe_descriptor_reuse_frame *batch;
  size_t offset;
  if (pool->leased || (pool->tail && frame < pool->tail->frame)) return NULL;
  if (!pool->frame_pages && !xe_descriptor_reuse_warm(pool)) return NULL;
  batch = pool->tail && pool->tail->frame == frame ? pool->tail : xe_descriptor_reuse_frame_take(pool, frame);
  if (!batch) return NULL;
  if (pool->free) {
    node = pool->free; pool->free = node->next;
  } else if (pool->unused) {
    node = pool->unused; pool->unused = node->next;
  } else {
    page = pool->cursor; offset = pool->next_slot;
    if (offset == XE_DESCRIPTOR_REUSE_PAGE_SLOTS) { page = page->next; offset = 0; }
    if (!page) {
      page = (xe_descriptor_reuse_page *)XE_DESCRIPTOR_REUSE_ALLOC(sizeof(*page));
      if (!page) {
        if (batch != pool->tail) { batch->next = pool->free_frames; pool->free_frames = batch; }
        return NULL;
      }
      page->next = NULL; pool->last_page->next = page; pool->last_page = page;
    }
    pool->cursor = page; pool->next_slot = offset + 1;
    node = page->nodes + offset; node->handle = 0;
  }
  pool->leased = node; pool->lease_frame = batch; return node;
}
static INLINE int xe_descriptor_reuse_commit(xe_descriptor_reuse *pool) {
  xe_descriptor_reuse_node *node = pool->leased;
  xe_descriptor_reuse_frame *batch = pool->lease_frame;
  if (!node || !node->handle) return 0;
  if (batch != pool->tail) {
    batch->next = NULL;
    if (pool->tail) pool->tail->next = batch;
    else pool->head = batch;
    pool->tail = batch;
  }
  /* Reverse within the frame, matching FIFO retirement followed by LIFO push. */
  node->next = batch->head; batch->head = node;
  if (!batch->tail) batch->tail = node;
  pool->leased = NULL; pool->lease_frame = NULL; return 1;
}
static INLINE void xe_descriptor_reuse_cancel(xe_descriptor_reuse *pool) {
  xe_descriptor_reuse_node *node = pool->leased;
  if (!node) return;
  if (node->handle) { node->next = pool->free; pool->free = node; }
  else { node->next = pool->unused; pool->unused = node; }
  pool->leased = NULL;
  if (pool->lease_frame != pool->tail) {
    pool->lease_frame->next = pool->free_frames; pool->free_frames = pool->lease_frame;
  }
  pool->lease_frame = NULL;
}
/* Return completed frame batches, not descriptors; the renderer needs neither
 * per-handle counters nor a per-handle walk at retirement. */
static INLINE size_t xe_descriptor_reuse_collect(xe_descriptor_reuse *pool, uint64_t completed) {
  xe_descriptor_reuse_frame *batch;
  size_t count = 0;
  while ((batch = pool->head) != NULL && batch->frame <= completed) {
    /* A lease may still append to the current tail. Keep that batch alive. */
    if (batch == pool->lease_frame) break;
    pool->head = batch->next;
    batch->tail->next = pool->free; pool->free = batch->head; ++count;
    batch->next = pool->free_frames; pool->free_frames = batch;
  }
  if (!pool->head) pool->tail = NULL;
  return count;
}
/* At the existing native-pool reset boundary only. Keep pages, forget handles
 * lazily when leasing retained virgin slots. No capacity-sized clear/copy. */
static INLINE int xe_descriptor_reuse_reset(xe_descriptor_reuse *pool) {
  if (pool->leased) return 0;
  pool->free = pool->unused = NULL; pool->head = pool->tail = pool->free_frames = pool->lease_frame = NULL;
  pool->cursor = pool->pages; pool->next_slot = 0;
  pool->frame_cursor = pool->frame_pages; pool->next_frame = 0; return 1;
}
static INLINE void xe_descriptor_reuse_destroy(xe_descriptor_reuse *pool) {
  xe_descriptor_reuse_page *page = pool->pages, *next;
  xe_descriptor_reuse_frame_page *frames = pool->frame_pages, *next_frames;
  while (page) { next = page->next; XE_DESCRIPTOR_REUSE_FREE(page); page = next; }
  while (frames) { next_frames = frames->next; XE_DESCRIPTOR_REUSE_FREE(frames); frames = next_frames; }
  pool->pages = pool->last_page = pool->cursor = NULL; pool->next_slot = 0;
  pool->frame_pages = pool->last_frame_page = pool->frame_cursor = NULL; pool->next_frame = 0;
  pool->free = pool->leased = pool->unused = NULL;
  pool->head = pool->tail = pool->free_frames = pool->lease_frame = NULL;
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
