#include "core_gpu_spans.h"
#include <string.h>

static uint32_t acquire_node(xe_gpu_span_pool *pool) {
   uint32_t index = pool->spare;
   pool->spare = pool->nodes[index].spare;
   --pool->available;
   memset(&pool->nodes[index], 0, sizeof(pool->nodes[index]));
   pool->nodes[index].prev = pool->nodes[index].next = XE_GPU_SPAN_NONE;
   pool->nodes[index].free_prev = pool->nodes[index].free_next = XE_GPU_SPAN_NONE;
   return index;
}
static void release_node(xe_gpu_span_pool *pool, uint32_t index) {
   pool->nodes[index].owner = NULL;
   pool->nodes[index].live = 0;
   pool->nodes[index].tag = 0;
   pool->nodes[index].spare = pool->spare;
   pool->spare = index;
   ++pool->available;
}
static unsigned size_class(uint64_t size) {
   unsigned result = 0;
   if (size >> 32) { size >>= 32; result += 32; }
   if (size >> 16) { size >>= 16; result += 16; }
   if (size >> 8) { size >>= 8; result += 8; }
   if (size >> 4) { size >>= 4; result += 4; }
   if (size >> 2) { size >>= 2; result += 2; }
   if (size >> 1) ++result;
   return result;
}
static unsigned first_class(uint64_t mask) {
   unsigned result = 0;
   if (!(mask & UINT64_C(0xffffffff))) { mask >>= 32; result += 32; }
   if (!(mask & UINT64_C(0xffff))) { mask >>= 16; result += 16; }
   if (!(mask & UINT64_C(0xff))) { mask >>= 8; result += 8; }
   if (!(mask & UINT64_C(0xf))) { mask >>= 4; result += 4; }
   if (!(mask & UINT64_C(3))) { mask >>= 2; result += 2; }
   if (!(mask & UINT64_C(1))) ++result;
   return result;
}
static void unlink_free(xe_gpu_span_arena *arena, uint32_t index) {
   xe_gpu_span_node *nodes = arena->pool->nodes, *node = &nodes[index];
   unsigned bin = size_class(node->size);
   if (node->free_prev != XE_GPU_SPAN_NONE) nodes[node->free_prev].free_next = node->free_next;
   else arena->free_heads[bin] = node->free_next;
   if (node->free_next != XE_GPU_SPAN_NONE) nodes[node->free_next].free_prev = node->free_prev;
   if (arena->free_heads[bin] == XE_GPU_SPAN_NONE) arena->free_mask &= ~(UINT64_C(1) << bin);
}
static void link_free(xe_gpu_span_arena *arena, uint32_t index) {
   xe_gpu_span_node *nodes = arena->pool->nodes, *node = &nodes[index];
   unsigned bin = size_class(node->size);
   node->free_prev = XE_GPU_SPAN_NONE;
   node->free_next = arena->free_heads[bin];
   if (arena->free_heads[bin] != XE_GPU_SPAN_NONE) nodes[arena->free_heads[bin]].free_prev = index;
   arena->free_heads[bin] = index;
   arena->free_mask |= UINT64_C(1) << bin;
}
int xe_gpu_span_pool_init(xe_gpu_span_pool *pool, xe_gpu_span_node *nodes, uint32_t capacity) {
   uint32_t i;
   size_t bytes;
   if (!pool || pool->nodes || !nodes || !capacity || capacity == XE_GPU_SPAN_NONE) return 0;
   bytes = (size_t)capacity * sizeof(*nodes);
   if (bytes / sizeof(*nodes) != capacity) return 0;
   memset(nodes, 0, bytes);
   pool->nodes = nodes; pool->capacity = pool->available = capacity;
   pool->spare = 0; pool->generation = 0;
   for (i = 0; i < capacity; ++i) nodes[i].spare = i + 1 < capacity ? i + 1 : XE_GPU_SPAN_NONE;
   return 1;
}
int xe_gpu_span_arena_init(xe_gpu_span_arena *arena, xe_gpu_span_pool *pool, uint64_t size) {
   uint32_t index, bin;
   if (!arena || arena->pool || !pool || !pool->nodes || !pool->available || !size) return 0;
   index = acquire_node(pool);
   arena->pool = pool; arena->size = size; arena->used = 0;
   arena->root = index; arena->free_mask = 0;
   for (bin = 0; bin < 64; ++bin) arena->free_heads[bin] = XE_GPU_SPAN_NONE;
   pool->nodes[index].owner = arena; pool->nodes[index].size = size;
   link_free(arena, index);
   return 1;
}
int xe_gpu_span_take(xe_gpu_span_arena *arena, uint64_t size, uint64_t alignment, xe_gpu_span *out) {
   xe_gpu_span_pool *pool;
   xe_gpu_span_node *nodes, *node, *extra;
   uint32_t i, best = XE_GPU_SPAN_NONE, index, old_prev, old_next, needed;
   uint64_t classes;
   unsigned bin;
   uint64_t aligned, pad, tail, waste, best_waste = UINT64_MAX, begin = 0, old_offset, old_size;
   if (!arena || !arena->pool || !out || !size || !alignment || (alignment & (alignment - 1))) return 0;
   pool = arena->pool; nodes = pool->nodes;
   if (pool->generation == UINT64_MAX) return 0;
   classes = arena->free_mask & (UINT64_MAX << size_class(size));
   while (classes) {
      bin = first_class(classes);
      for (i = arena->free_heads[bin]; i != XE_GPU_SPAN_NONE; i = nodes[i].free_next) {
         node = &nodes[i];
         if (node->offset > UINT64_MAX - (alignment - 1)) continue;
         aligned = (node->offset + alignment - 1) & ~(alignment - 1);
         pad = aligned - node->offset;
         if (pad > node->size || size > node->size - pad) continue;
         tail = node->size - pad - size;
         needed = (pad != 0) + (tail != 0);
         if (needed > pool->available) continue;
         waste = node->size - size;
         if (best == XE_GPU_SPAN_NONE || waste < best_waste) {
            best = i; best_waste = waste; begin = aligned;
            if (!waste) break;
         }
      }
      if (best != XE_GPU_SPAN_NONE) break;
      classes &= ~(UINT64_C(1) << bin);
   }
   if (best == XE_GPU_SPAN_NONE) return 0;
   node = &nodes[best]; old_prev = node->prev; old_next = node->next;
   old_offset = node->offset; old_size = node->size;
   pad = begin - old_offset; tail = old_size - pad - size;
   unlink_free(arena, best);
   node->offset = begin; node->size = size; node->live = 1;
   node->tag = ++pool->generation;
   if (pad) {
      index = acquire_node(pool); extra = &nodes[index];
      extra->owner = arena; extra->offset = old_offset; extra->size = pad;
      extra->prev = old_prev; extra->next = best; node->prev = index;
      if (old_prev != XE_GPU_SPAN_NONE) nodes[old_prev].next = index;
      else arena->root = index;
      link_free(arena, index);
   }
   if (tail) {
      index = acquire_node(pool); extra = &nodes[index];
      extra->owner = arena; extra->offset = begin + size; extra->size = tail;
      extra->prev = best; extra->next = old_next; node->next = index;
      if (old_next != XE_GPU_SPAN_NONE) nodes[old_next].prev = index;
      link_free(arena, index);
   }
   arena->used += size;
   out->offset = begin; out->size = size; out->tag = node->tag; out->slot = best;
   return 1;
}
int xe_gpu_span_return(xe_gpu_span_arena *arena, const xe_gpu_span *span) {
   xe_gpu_span_pool *pool;
   xe_gpu_span_node *nodes, *node;
   uint32_t index, neighbor;
   if (!arena || !arena->pool || !span) return 0;
   pool = arena->pool;
   if (span->slot >= pool->capacity) return 0;
   nodes = pool->nodes; index = span->slot; node = &nodes[index];
   if (node->owner != arena || !node->live || !span->tag || node->tag != span->tag ||
       node->offset != span->offset || node->size != span->size) return 0;
   arena->used -= node->size; node->live = 0; node->tag = 0;
   neighbor = node->prev;
   if (neighbor != XE_GPU_SPAN_NONE && !nodes[neighbor].live) {
      unlink_free(arena, neighbor);
      nodes[neighbor].size += node->size; nodes[neighbor].next = node->next;
      if (node->next != XE_GPU_SPAN_NONE) nodes[node->next].prev = neighbor;
      release_node(pool, index); index = neighbor; node = &nodes[index];
   }
   neighbor = node->next;
   if (neighbor != XE_GPU_SPAN_NONE && !nodes[neighbor].live) {
      unlink_free(arena, neighbor);
      node->size += nodes[neighbor].size; node->next = nodes[neighbor].next;
      if (node->next != XE_GPU_SPAN_NONE) nodes[node->next].prev = index;
      release_node(pool, neighbor);
   }
   link_free(arena, index);
   return 1;
}
int xe_gpu_span_arena_close(xe_gpu_span_arena *arena) {
   if (!arena || !arena->pool || arena->used) return 0;
   release_node(arena->pool, arena->root);
   memset(arena, 0, sizeof(*arena));
   return 1;
}
