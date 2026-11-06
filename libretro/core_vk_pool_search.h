/* Single-owner descriptor search metadata. Nodes are retained and never move.
 * Intrusive treap preserves multimap order, including newest equal-key first
 * on descending traversal. No allocation/copy for insert, erase or hot reuse. */
#ifndef XE_CORE_VK_POOL_SEARCH_H
#define XE_CORE_VK_POOL_SEARCH_H
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <vulkan/vulkan_core.h>
#include <retro_inline.h>
#ifndef XE_POOL_SEARCH_ALLOC
#define XE_POOL_SEARCH_ALLOC malloc
#define XE_POOL_SEARCH_FREE free
#endif
enum { XE_POOL_SEARCH_SLOTS = 32 };
typedef struct xe_vk_pool_search_node {
  struct xe_vk_pool_search_node *left, *right, *parent;
  VkDescriptorPool pool;
  uint32_t *descriptors_remaining;
  uint32_t descriptor_sets_remaining, key, priority;
} xe_vk_pool_search_node;
typedef struct xe_vk_pool_search_page {
  struct xe_vk_pool_search_page *next;
  xe_vk_pool_search_node nodes[XE_POOL_SEARCH_SLOTS];
} xe_vk_pool_search_page;
typedef struct xe_vk_pool_search {
  xe_vk_pool_search_page *pages, *last, *cursor;
  size_t next_slot;
  xe_vk_pool_search_node *root, *maximum, *free, *leased;
  uint32_t random;
} xe_vk_pool_search;
static INLINE xe_vk_pool_search_node *xe_vk_pool_search_acquire(xe_vk_pool_search *s) {
  xe_vk_pool_search_page *page;
  xe_vk_pool_search_node *node;
  size_t slot;
  if (s->leased) return NULL;
  if (s->free) { node = s->free; s->free = node->left; }
  else {
    page = s->cursor; slot = s->next_slot;
    if (page && slot == XE_POOL_SEARCH_SLOTS) { page = page->next; slot = 0; }
    if (!page) {
      page = (xe_vk_pool_search_page *)XE_POOL_SEARCH_ALLOC(sizeof(*page));
      if (!page) return NULL;
      page->next = NULL;
      if (s->last) s->last->next = page;
      else s->pages = page;
      s->last = page;
    }
    node = page->nodes + slot; s->cursor = page; s->next_slot = slot + 1;
  }
  s->leased = node; return node;
}
static INLINE void xe_vk_pool_search_cancel(xe_vk_pool_search *s) {
  if (!s->leased) return;
  s->leased->left = s->free; s->free = s->leased; s->leased = NULL;
}
static INLINE xe_vk_pool_search_node *xe_vk_pool_search_commit(xe_vk_pool_search *s) {
  xe_vk_pool_search_node *node = s->leased;
  s->leased = NULL; return node;
}
static INLINE int xe_vk_pool_search_warm(xe_vk_pool_search *s) {
  if (!xe_vk_pool_search_acquire(s)) return 0;
  xe_vk_pool_search_cancel(s); return 1;
}
/* Node must have been detached from the tree and from the caller's latest slot. */
static INLINE void xe_vk_pool_search_release(xe_vk_pool_search *s, xe_vk_pool_search_node *node) {
  node->left = s->free; s->free = node;
}
static INLINE void xe_vk_pool_search_rotate(xe_vk_pool_search *s, xe_vk_pool_search_node *node, int left) {
  xe_vk_pool_search_node *child = left ? node->right : node->left;
  if (left) {
    node->right = child->left;
    if (node->right) node->right->parent = node;
    child->left = node;
  } else {
    node->left = child->right;
    if (node->left) node->left->parent = node;
    child->right = node;
  }
  child->parent = node->parent;
  if (!node->parent) s->root = child;
  else if (node == node->parent->left) node->parent->left = child;
  else node->parent->right = child;
  node->parent = child;
}
static INLINE void xe_vk_pool_search_insert(xe_vk_pool_search *s, xe_vk_pool_search_node *node, uint32_t key) {
  xe_vk_pool_search_node *parent = NULL, *at = s->root;
  uint32_t random = s->random ? s->random : UINT32_C(0x6d2b79f5);
  random ^= random << 13; random ^= random >> 17; random ^= random << 5; s->random = random;
  node->key = key; node->priority = random; node->left = node->right = NULL;
  while (at) { parent = at; at = key < at->key ? at->left : at->right; }
  node->parent = parent;
  if (!parent) s->root = node;
  else if (key < parent->key) parent->left = node;
  else parent->right = node;
  if (!s->maximum || key >= s->maximum->key) s->maximum = node;
  while (node->parent && node->priority < node->parent->priority)
    xe_vk_pool_search_rotate(s, node->parent, node == node->parent->right);
}
static INLINE xe_vk_pool_search_node *xe_vk_pool_search_previous(xe_vk_pool_search_node *node) {
  xe_vk_pool_search_node *parent;
  if (node->left) {
    node = node->left; while (node->right) node = node->right; return node;
  }
  while ((parent = node->parent) != NULL && node == parent->left) node = parent;
  return parent;
}
static INLINE void xe_vk_pool_search_erase(xe_vk_pool_search *s, xe_vk_pool_search_node *node) {
  xe_vk_pool_search_node *child;
  if (s->maximum == node) s->maximum = xe_vk_pool_search_previous(node);
  while (node->left && node->right)
    xe_vk_pool_search_rotate(s, node, node->right->priority < node->left->priority);
  child = node->left ? node->left : node->right;
  if (child) child->parent = node->parent;
  if (!node->parent) s->root = child;
  else if (node == node->parent->left) node->parent->left = child;
  else node->parent->right = child;
  node->left = node->right = node->parent = NULL;
}
/* Caller forgets its latest pointer at the same reset boundary. */
static INLINE int xe_vk_pool_search_reset(xe_vk_pool_search *s) {
  if (s->leased) return 0;
  s->root = s->maximum = s->free = NULL; s->cursor = s->pages; s->next_slot = 0;
  s->random = 0; return 1;
}
static INLINE int xe_vk_pool_search_destroy(xe_vk_pool_search *s) {
  xe_vk_pool_search_page *page = s->pages, *next;
  if (!xe_vk_pool_search_reset(s)) return 0;
  while (page) { next = page->next; XE_POOL_SEARCH_FREE(page); page = next; }
  s->pages = s->last = s->cursor = NULL; return 1;
}
#endif
