/* Independent byte-occupancy model; no GPU, malloc, vectors or threads. C89. */
#include <stdio.h>
#include <string.h>
#include "core_gpu_spans.h"
#define CAPACITY 257
#define BYTES 4096
#define SLOTS 64
static xe_gpu_span_node nodes[CAPACITY];
static xe_gpu_span handles[SLOTS];
static unsigned char occupied[BYTES], active[SLOTS];
static uint32_t rng = 1234567;
static uint32_t random_word(void) { rng = rng * UINT32_C(1664525) + UINT32_C(1013904223); return rng; }
static int validate(const xe_gpu_span_arena *arena) {
   const xe_gpu_span_node *node;
   uint32_t i = arena->root, prev = XE_GPU_SPAN_NONE, count = 0, free_count = 0;
   uint64_t end = 0, used = 0, mask = 0, size;
   unsigned bin, expected_bin;
   int previous_free = 0;
   while (i != XE_GPU_SPAN_NONE) {
      if (i >= arena->pool->capacity || ++count > arena->pool->capacity) return 0;
      node = &arena->pool->nodes[i];
      if (node->owner != arena || node->prev != prev || node->offset != end || !node->size ||
          node->size > arena->size - end || (previous_free && !node->live)) return 0;
      end += node->size;
      if (node->live) used += node->size; else ++free_count;
      previous_free = !node->live; prev = i; i = node->next;
   }
   if (end != arena->size || used != arena->used) return 0;
   count = 0;
   for (bin = 0; bin < 64; ++bin) {
      i = arena->free_heads[bin]; prev = XE_GPU_SPAN_NONE;
      if (i != XE_GPU_SPAN_NONE) mask |= UINT64_C(1) << bin;
      while (i != XE_GPU_SPAN_NONE) {
         if (i >= arena->pool->capacity || ++count > free_count) return 0;
         node = &arena->pool->nodes[i];
         size = node->size; expected_bin = 0;
         while (size >>= 1) ++expected_bin;
         if (expected_bin != bin || node->owner != arena || node->live || node->free_prev != prev) return 0;
         prev = i; i = node->free_next;
      }
   }
   if (mask != arena->free_mask) return 0;
   return count == free_count;
}
static int randomized(void) {
   xe_gpu_span_pool pool;
   xe_gpu_span_arena arena, other;
   xe_gpu_span span, bad;
   uint64_t size, alignment, generation, used;
   uint32_t available;
   unsigned round, slot, i;
   memset(&pool, 0, sizeof(pool)); memset(&arena, 0, sizeof(arena)); memset(&other, 0, sizeof(other));
   if (!xe_gpu_span_pool_init(&pool, nodes, CAPACITY) ||
       !xe_gpu_span_arena_init(&arena, &pool, BYTES) ||
       !xe_gpu_span_arena_init(&other, &pool, 256)) return 0;
   for (round = 0; round < 300000; ++round) {
      slot = random_word() % SLOTS;
      if (active[slot]) {
         span = handles[slot];
         if (xe_gpu_span_return(&other, &span) || xe_gpu_span_arena_close(&arena)) return 0;
         bad = span; ++bad.size;
         if (xe_gpu_span_return(&arena, &bad)) return 0;
         for (i = 0; i < span.size; ++i) {
            if (!occupied[(size_t)span.offset + i]) return 0;
            occupied[(size_t)span.offset + i] = 0;
         }
         if (!xe_gpu_span_return(&arena, &span) || xe_gpu_span_return(&arena, &span)) return 0;
         active[slot] = 0;
      } else {
         size = 1 + random_word() % 129;
         alignment = UINT64_C(1) << (random_word() % 9);
         generation = pool.generation; available = pool.available; used = arena.used;
         if (xe_gpu_span_take(&arena, size, alignment, &span)) {
            if (span.offset % alignment || span.size != size || span.offset > BYTES || size > BYTES - span.offset) return 0;
            for (i = 0; i < size; ++i) {
               if (occupied[(size_t)span.offset + i]) return 0;
               occupied[(size_t)span.offset + i] = 1;
            }
            handles[slot] = span; active[slot] = 1;
         } else if (pool.generation != generation || pool.available != available || arena.used != used) return 0;
      }
      if (!validate(&arena) || !validate(&other)) return 0;
   }
   for (slot = 0; slot < SLOTS; ++slot) if (active[slot] && !xe_gpu_span_return(&arena, &handles[slot])) return 0;
   if (!validate(&arena) || arena.used || pool.available != CAPACITY - 2) return 0;
   if (!xe_gpu_span_arena_close(&arena) || !xe_gpu_span_arena_close(&other) || pool.available != CAPACITY) return 0;
   return 1;
}
static int boundaries(void) {
   xe_gpu_span_node storage[4];
   xe_gpu_span_pool pool;
   xe_gpu_span_arena arena;
   xe_gpu_span first, stale, current;
   memset(&pool, 0, sizeof(pool)); memset(&arena, 0, sizeof(arena));
   if (!xe_gpu_span_pool_init(&pool, storage, 2) || !xe_gpu_span_arena_init(&arena, &pool, 64)) return 0;
   if (!xe_gpu_span_take(&arena, 1, 1, &first) || xe_gpu_span_take(&arena, 1, 2, &current) ||
       !validate(&arena) || arena.used != 1 || !xe_gpu_span_return(&arena, &first)) return 0;
   stale = first;
   if (!xe_gpu_span_take(&arena, 1, 1, &current) || xe_gpu_span_return(&arena, &stale) ||
       !xe_gpu_span_return(&arena, &current) || !xe_gpu_span_arena_close(&arena)) return 0;
   if (!xe_gpu_span_arena_init(&arena, &pool, UINT64_MAX)) return 0;
   if (xe_gpu_span_take(&arena, 1, 0, &first) || xe_gpu_span_take(&arena, 1, 3, &first) ||
       xe_gpu_span_take(&arena, 0, 1, &first)) return 0;
   if (!xe_gpu_span_take(&arena, UINT64_MAX - 3, 1, &first) ||
       xe_gpu_span_take(&arena, 1, 8, &current) || !xe_gpu_span_return(&arena, &first)) return 0;
   pool.generation = UINT64_MAX;
   if (xe_gpu_span_take(&arena, 1, 1, &first) || !validate(&arena) || !xe_gpu_span_arena_close(&arena)) return 0;
   return pool.available == pool.capacity;
}
int main(void) {
   if (!randomized()) return 1;
   if (!boundaries()) return 2;
   puts("PASS: 300000 modeled GPU span operations, alignment, bounds, coalescing, exhaustion and stale handles");
   return 0;
}
