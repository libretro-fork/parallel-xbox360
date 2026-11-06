/* Single-owner keyed GPU-handle reuse. Zero-initialize; never copy a live owner.
 * CPU records never own native handles.
 * Frame batches reverse each key's FIFO use order, allowing completed batches
 * to splice onto that key's free LIFO without walking or copying handles. */
#ifndef XE_CORE_KEYED_DESCRIPTOR_REUSE_H
#define XE_CORE_KEYED_DESCRIPTOR_REUSE_H
#include <string.h>
#include "core_descriptor_reuse.h"
#ifndef XE_KEYED_REUSE_ALLOC
#define XE_KEYED_REUSE_ALLOC malloc
#define XE_KEYED_REUSE_FREE free
#endif
enum { XE_KEYED_REUSE_KEYS = 32, XE_KEYED_REUSE_FRAMES = 16 };
typedef struct xe_keyed_reuse_frame {
  struct xe_keyed_reuse_frame *next;
  uint64_t frame;
  xe_descriptor_reuse_node *head, *tail;
  struct xe_keyed_reuse_entry *entry;
} xe_keyed_reuse_frame;
typedef struct xe_keyed_reuse_entry {
  xe_descriptor_reuse_node *free;
  xe_keyed_reuse_frame *tail;
  uint64_t epoch;
  uint32_t key;
} xe_keyed_reuse_entry;
typedef struct xe_keyed_reuse_key_page {
  struct xe_keyed_reuse_key_page *next;
  xe_keyed_reuse_entry entries[XE_KEYED_REUSE_KEYS];
} xe_keyed_reuse_key_page;
typedef struct xe_keyed_reuse_frame_page {
  struct xe_keyed_reuse_frame_page *next;
  xe_keyed_reuse_frame frames[XE_KEYED_REUSE_FRAMES];
} xe_keyed_reuse_frame_page;
typedef struct xe_keyed_descriptor_reuse {
  xe_keyed_reuse_key_page *keys, *last_key_page, *key_cursor;
  size_t next_key;
  xe_descriptor_reuse_page *nodes, *last_node_page, *node_cursor;
  size_t next_node;
  xe_keyed_reuse_frame_page *frames, *last_frame_page, *frame_cursor;
  size_t next_frame;
  xe_keyed_reuse_entry **table;
  size_t capacity, key_count;
  xe_keyed_reuse_entry *lease_entry, *recent[2];
  xe_descriptor_reuse_node *unused, *leased;
  xe_keyed_reuse_frame *head, *tail, *free_frames, *lease_frame;
  uint64_t epoch;
  unsigned recent_slot;
} xe_keyed_descriptor_reuse;
static INLINE uint32_t xe_keyed_reuse_hash(uint32_t key) {
  key ^= key >> 16; key *= UINT32_C(0x7feb352d);
  key ^= key >> 15; key *= UINT32_C(0x846ca68b); return key ^ (key >> 16);
}
static INLINE int xe_keyed_reuse_grow_table(xe_keyed_descriptor_reuse *s) {
  size_t capacity, i, at;
  xe_keyed_reuse_entry **table, *entry;
  if (s->capacity > (size_t)-1 / 2) return 0;
  capacity = s->capacity ? s->capacity * 2 : 128;
  if (capacity > (size_t)-1 / sizeof(*table)) return 0;
  table = (xe_keyed_reuse_entry **)XE_KEYED_REUSE_ALLOC(capacity * sizeof(*table));
  if (!table) return 0;
  memset(table, 0, capacity * sizeof(*table));
  for (i = 0; i < s->capacity; ++i) if ((entry = s->table[i]) != NULL) {
    at = xe_keyed_reuse_hash(entry->key) & (capacity - 1);
    while (table[at]) at = (at + 1) & (capacity - 1);
    table[at] = entry;
  }
  XE_KEYED_REUSE_FREE(s->table); s->table = table; s->capacity = capacity; return 1;
}
/* Partial failure leaves every successful allocation owned for retry/destruction. */
static INLINE int xe_keyed_reuse_warm(xe_keyed_descriptor_reuse *s) {
  if (!s->table && !xe_keyed_reuse_grow_table(s)) return 0;
  if (!s->keys) {
    s->keys = (xe_keyed_reuse_key_page *)XE_KEYED_REUSE_ALLOC(sizeof(*s->keys));
    if (!s->keys) return 0;
    s->keys->next = NULL; s->last_key_page = s->key_cursor = s->keys; s->next_key = 0;
  }
  if (!s->nodes) {
    s->nodes = (xe_descriptor_reuse_page *)XE_KEYED_REUSE_ALLOC(sizeof(*s->nodes));
    if (!s->nodes) return 0;
    s->nodes->next = NULL; s->last_node_page = s->node_cursor = s->nodes; s->next_node = 0;
  }
  if (!s->frames) {
    s->frames = (xe_keyed_reuse_frame_page *)XE_KEYED_REUSE_ALLOC(sizeof(*s->frames));
    if (!s->frames) return 0;
    s->frames->next = NULL; s->last_frame_page = s->frame_cursor = s->frames; s->next_frame = 0;
  }
  if (!s->epoch) s->epoch = 1;
  return 1;
}
static INLINE xe_keyed_reuse_entry *xe_keyed_reuse_entry_get(xe_keyed_descriptor_reuse *s, uint32_t key) {
  xe_keyed_reuse_entry *entry;
  xe_keyed_reuse_key_page *page;
  size_t at, slot;
  if (s->recent[0] && s->recent[0]->key == key) return s->recent[0];
  if (s->recent[1] && s->recent[1]->key == key) return s->recent[1];
  at = xe_keyed_reuse_hash(key) & (s->capacity - 1);
  while ((entry = s->table[at]) != NULL) {
    if (entry->key == key) break;
    at = (at + 1) & (s->capacity - 1);
  }
  if (!entry) {
    if (s->key_count >= s->capacity - s->capacity / 4) {
      if (!xe_keyed_reuse_grow_table(s)) return NULL;
      at = xe_keyed_reuse_hash(key) & (s->capacity - 1);
      while (s->table[at]) at = (at + 1) & (s->capacity - 1);
    }
    page = s->key_cursor; slot = s->next_key;
    if (slot == XE_KEYED_REUSE_KEYS) { page = page->next; slot = 0; }
    if (!page) {
      page = (xe_keyed_reuse_key_page *)XE_KEYED_REUSE_ALLOC(sizeof(*page));
      if (!page) return NULL;
      page->next = NULL; s->last_key_page->next = page; s->last_key_page = page;
    }
    entry = page->entries + slot; s->key_cursor = page; s->next_key = slot + 1;
    entry->key = key; entry->epoch = 0; s->table[at] = entry; ++s->key_count;
  }
  s->recent[s->recent_slot] = entry; s->recent_slot ^= 1;
  return entry;
}
static INLINE xe_keyed_reuse_frame *xe_keyed_reuse_frame_take(xe_keyed_descriptor_reuse *s, xe_keyed_reuse_entry *entry, uint64_t frame) {
  xe_keyed_reuse_frame *batch;
  xe_keyed_reuse_frame_page *page;
  size_t slot;
  if (s->free_frames) { batch = s->free_frames; s->free_frames = batch->next; }
  else {
    page = s->frame_cursor; slot = s->next_frame;
    if (slot == XE_KEYED_REUSE_FRAMES) { page = page->next; slot = 0; }
    if (!page) {
      page = (xe_keyed_reuse_frame_page *)XE_KEYED_REUSE_ALLOC(sizeof(*page));
      if (!page) return NULL;
      page->next = NULL; s->last_frame_page->next = page; s->last_frame_page = page;
    }
    batch = page->frames + slot; s->frame_cursor = page; s->next_frame = slot + 1;
  }
  batch->entry = entry; batch->frame = frame; batch->head = batch->tail = NULL; return batch;
}
static INLINE xe_descriptor_reuse_node *xe_keyed_reuse_acquire(xe_keyed_descriptor_reuse *s, uint32_t key, uint64_t frame) {
  xe_keyed_reuse_entry *entry;
  xe_keyed_reuse_frame *batch;
  xe_descriptor_reuse_page *page;
  xe_descriptor_reuse_node *node;
  size_t slot;
  if (s->leased || (s->tail && frame < s->tail->frame)) return NULL;
  if (!s->epoch && !xe_keyed_reuse_warm(s)) return NULL;
  entry = xe_keyed_reuse_entry_get(s, key); if (!entry) return NULL;
  if (entry->epoch != s->epoch) {
    entry->free = NULL; entry->tail = NULL; entry->epoch = s->epoch;
  }
  batch = entry->tail && entry->tail->frame == frame ? entry->tail : xe_keyed_reuse_frame_take(s, entry, frame);
  if (!batch) return NULL;
  if (entry->free) { node = entry->free; entry->free = node->next; }
  else if (s->unused) { node = s->unused; s->unused = node->next; }
  else {
    page = s->node_cursor; slot = s->next_node;
    if (slot == XE_DESCRIPTOR_REUSE_PAGE_SLOTS) { page = page->next; slot = 0; }
    if (!page) {
      page = (xe_descriptor_reuse_page *)XE_KEYED_REUSE_ALLOC(sizeof(*page));
      if (!page) {
        if (batch != entry->tail) { batch->next = s->free_frames; s->free_frames = batch; }
        return NULL;
      }
      page->next = NULL; s->last_node_page->next = page; s->last_node_page = page;
    }
    node = page->nodes + slot; s->node_cursor = page; s->next_node = slot + 1; node->handle = 0;
  }
  s->leased = node; s->lease_entry = entry; s->lease_frame = batch; return node;
}
static INLINE int xe_keyed_reuse_commit(xe_keyed_descriptor_reuse *s) {
  xe_descriptor_reuse_node *node = s->leased;
  xe_keyed_reuse_entry *entry = s->lease_entry;
  xe_keyed_reuse_frame *batch = s->lease_frame;
  if (!node || !node->handle) return 0;
  if (batch != entry->tail) {
    batch->next = NULL;
    if (s->tail) s->tail->next = batch;
    else s->head = batch;
    s->tail = batch; entry->tail = batch;
  }
  node->next = batch->head; batch->head = node;
  if (!batch->tail) batch->tail = node;
  s->leased = NULL; s->lease_entry = NULL; s->lease_frame = NULL; return 1;
}
static INLINE void xe_keyed_reuse_cancel(xe_keyed_descriptor_reuse *s) {
  xe_descriptor_reuse_node *node = s->leased;
  xe_keyed_reuse_entry *entry = s->lease_entry;
  if (!node) return;
  if (node->handle) { node->next = entry->free; entry->free = node; }
  else { node->next = s->unused; s->unused = node; }
  if (s->lease_frame != entry->tail) { s->lease_frame->next = s->free_frames; s->free_frames = s->lease_frame; }
  s->leased = NULL; s->lease_entry = NULL; s->lease_frame = NULL;
}
/* Frames follow the original global FIFO's monotonically ordered contract.
 * Only completed key/frame batches are visited; future/historical keys are not
 * scanned. A referenced pending batch stays alive until its lease ends. */
static INLINE size_t xe_keyed_reuse_collect(xe_keyed_descriptor_reuse *s, uint64_t completed) {
  xe_keyed_reuse_entry *entry;
  xe_keyed_reuse_frame *batch;
  size_t batches = 0;
  while ((batch = s->head) != NULL && batch->frame <= completed && batch != s->lease_frame) {
    s->head = batch->next; entry = batch->entry;
    if (entry->tail == batch) entry->tail = NULL;
    batch->tail->next = entry->free; entry->free = batch->head;
    batch->next = s->free_frames; s->free_frames = batch; ++batches;
  }
  if (!s->head) s->tail = NULL;
  return batches;
}
/* Existing native-pool-reset boundary only. Epochs lazily forget invalid handles;
 * keys/table/pages remain stable. The exceptional epoch-wrap sweep is cold. */
static INLINE int xe_keyed_reuse_reset(xe_keyed_descriptor_reuse *s) {
  xe_keyed_reuse_key_page *page;
  size_t left, n, i;
  if (s->leased) return 0;
  if (s->epoch && ++s->epoch == 0) {
    left = s->key_count;
    for (page = s->keys; left; page = page->next) {
      n = left < XE_KEYED_REUSE_KEYS ? left : (size_t)XE_KEYED_REUSE_KEYS;
      for (i = 0; i < n; ++i) page->entries[i].epoch = 0;
      left -= n;
    }
    s->epoch = 1;
  }
  s->head = s->tail = NULL; s->unused = NULL; s->free_frames = NULL;
  s->node_cursor = s->nodes; s->next_node = 0; s->frame_cursor = s->frames; s->next_frame = 0;
  return 1;
}
static INLINE int xe_keyed_reuse_destroy(xe_keyed_descriptor_reuse *s) {
  xe_keyed_reuse_key_page *key = s->keys, *next_key;
  xe_descriptor_reuse_page *node = s->nodes, *next_node;
  xe_keyed_reuse_frame_page *frame = s->frames, *next_frame;
  if (s->leased) return 0;
  while (key) { next_key = key->next; XE_KEYED_REUSE_FREE(key); key = next_key; }
  while (node) { next_node = node->next; XE_KEYED_REUSE_FREE(node); node = next_node; }
  while (frame) { next_frame = frame->next; XE_KEYED_REUSE_FREE(frame); frame = next_frame; }
  XE_KEYED_REUSE_FREE(s->table); memset(s, 0, sizeof(*s)); return 1;
}
#ifdef __cplusplus
struct xe_keyed_reuse_scope {
  xe_keyed_descriptor_reuse *pool;
  explicit xe_keyed_reuse_scope(xe_keyed_descriptor_reuse *p) : pool(p) {}
  xe_keyed_reuse_scope(const xe_keyed_reuse_scope&) = delete;
  xe_keyed_reuse_scope& operator=(const xe_keyed_reuse_scope&) = delete;
  ~xe_keyed_reuse_scope() { xe_keyed_reuse_cancel(pool); }
};
#endif
#endif
