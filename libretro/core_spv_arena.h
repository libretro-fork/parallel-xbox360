/* Single-owner reusable storage. Retire explicit typed owners before terminal
 * cleanup of remaining records. Node storage is retained until close. */
#ifndef XE_CORE_SPV_ARENA_H
#define XE_CORE_SPV_ARENA_H
#include <stddef.h>
#include <stdint.h>
#include <retro_inline.h>
#include "core_spv_index.h"
#include "core_spv_constant_cache.h"
#include "core_spv_groups.h"
#include "core_spv_stack.h"
#include "core_spv_u32_set.h"
#include "core_spv_extensions.h"
#include "core_spv_decorations.h"
#if defined(__SANITIZE_ADDRESS__)
#define XE_SPV_ARENA_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define XE_SPV_ARENA_ASAN 1
#endif
#endif
#ifdef __cplusplus
extern "C" {
#endif
enum { XE_SPV_RECORD_BYTES = 64, XE_SPV_RECORDS_PER_PAGE = 128,
       XE_SPV_RECORD_ARENA_OFFSET = 12 };
struct xe_spv_arena_page;
struct xe_spv_object_state;
typedef struct xe_spv_arena {
   struct xe_spv_arena_page *head, *tail, *cursor;
   size_t reserved_records;
   xe_spv_index index;
   int in_use;
   struct xe_spv_object_state *objects;
} xe_spv_arena;
/* Exclusive module-owned CFG edges. Contiguous borrowed prefixes retain exact
 * insertion order/duplicates; relocation, clear and module end expire views.
 * No shallow copies. Growth uses retained payload pages, not per-block heaps. */
typedef struct xe_spv_edges {
   void **records;
   size_t count, capacity;
} xe_spv_edges;
int xe_spv_arena_edges_grow_push(xe_spv_arena *arena, xe_spv_edges *edges, void *target);
static INLINE int xe_spv_arena_edges_push(xe_spv_arena *arena,
      xe_spv_edges *edges, void *target)
{
   if (!arena || !arena->in_use || !target) return 0;
   if (edges->count < edges->capacity) {
      edges->records[edges->count++] = target;
      return 1;
   }
   return xe_spv_arena_edges_grow_push(arena, edges, target);
}
static INLINE xe_spv_record_view xe_spv_edges_view(const xe_spv_edges *edges)
{
   xe_spv_record_view view;
   view.records = edges->records; view.count = edges->count;
   return view;
}
/* Clear before typed retirement without consulting the (possibly dead) parent. */
void xe_spv_arena_edges_clear(xe_spv_edges *edges);

enum { XE_SPV_OBJECT_BLOCK = 0, XE_SPV_OBJECT_FUNCTION = 1 };
/* Exclusive module lease; object addresses never move. Constructors must retire
 * their allocation on failure. Typed deletion retires after payload destruction. */
/* Cold minima only; larger valid modules grow retained pages without moving records. */
int xe_spv_arena_reserve_objects(xe_spv_arena *arena, size_t block_bytes,
      size_t blocks, size_t function_bytes, size_t functions);
void *xe_spv_arena_object_alloc(xe_spv_arena *arena, unsigned kind, size_t bytes);
void xe_spv_arena_object_retire(void *record);
/* Destroy/retire remaining functions before blocks, before instruction cleanup.
 * Callbacks may retire child objects; they must not throw or allocate new ones. */
int xe_spv_arena_objects_end(xe_spv_arena *arena,
      void (*destroy_function)(void *), void (*destroy_block)(void *));
int xe_spv_arena_reserve(xe_spv_arena *arena, size_t records);
int xe_spv_arena_begin(xe_spv_arena *arena);
void *xe_spv_arena_alloc(xe_spv_arena *arena, uint32_t *arena_offset);
/* destroy must retire its record; null is accepted only if all are retired. */
int xe_spv_arena_end(xe_spv_arena *arena, void (*destroy)(void *record));
/* Remaining records are arena-owned. Callback only destroys payload; it must
 * not allocate, retire or access other records in this arena, or throw. No live
 * external typed owner may remain. The arena retires/poisons the records. */
int xe_spv_arena_finish(xe_spv_arena *arena, void (*destroy_payload)(void *record));
int xe_spv_arena_close(xe_spv_arena *arena);
void xe_spv_arena_retire(void *record);
/* Payload workspace remains exclusive to the same active module lease. The
 * resize callback may extend its latest live allocation without moving words. */
int xe_spv_arena_reserve_payloads(xe_spv_arena *arena, size_t bytes);
/* Borrowed only for this module lease. extra reserves publication slots without
 * copying operands; capacity survives subsequent leases until arena close. */
xe_spv_constant_cache *xe_spv_arena_constants(xe_spv_arena *arena, size_t extra);
int xe_spv_arena_reserve_constants(xe_spv_arena *arena, size_t count);
xe_spv_record_view xe_spv_arena_group_view(const xe_spv_arena *arena, uint32_t key);
int xe_spv_arena_group_append(xe_spv_arena *arena, uint32_t key, void *record);
int xe_spv_arena_reserve_type_groups(xe_spv_arena *arena);
int xe_spv_arena_reserve_extensions(xe_spv_arena *arena, size_t count);
int xe_spv_arena_add_extension(xe_spv_arena *arena, const char *name);
void xe_spv_arena_remove_extension(xe_spv_arena *arena, const char *name);
/* View includes disabled cached names; only active entries are emitted.
 * All records/word spans expire at lease reset/close; metadata view also on add. */
xe_spv_record_view xe_spv_arena_extensions(const xe_spv_arena *arena);
int xe_spv_arena_reserve_decorations(xe_spv_arena *arena, size_t count);
int xe_spv_arena_add_decoration(xe_spv_arena *arena, const xe_spv_decoration_key *key);
/* Same lease/metadata invalidation as extension views. All records are emitted. */
xe_spv_record_view xe_spv_arena_decorations(const xe_spv_arena *arena);
enum { XE_SPV_STACK_SCOPE = 0, XE_SPV_STACK_SWITCH = 1, XE_SPV_STACK_LOOP = 2, XE_SPV_STACK_COUNT = 3 };
int xe_spv_arena_reserve_stacks(xe_spv_arena *arena, size_t loop_bytes);
void *xe_spv_arena_stack_push(xe_spv_arena *arena, unsigned kind, size_t bytes);
void *xe_spv_arena_stack_top(const xe_spv_arena *arena, unsigned kind);
int xe_spv_arena_stack_pop(xe_spv_arena *arena, unsigned kind);
void xe_spv_arena_stack_clear(xe_spv_arena *arena, unsigned kind);
void xe_spv_arena_clear_stacks(xe_spv_arena *arena);
int xe_spv_arena_reserve_capabilities(xe_spv_arena *arena, size_t count);
int xe_spv_arena_add_capability(xe_spv_arena *arena, uint32_t value);
int xe_spv_arena_has_capability(const xe_spv_arena *arena, uint32_t value);
xe_spv_word_view xe_spv_arena_capabilities(const xe_spv_arena *arena);
void *xe_spv_arena_record_owner(const void *record, uint32_t offset);
void *xe_spv_arena_payload_realloc(void *context, void *old, size_t old_bytes, size_t bytes);
/* Checks live instruction slots without reading candidate payload bytes. */
int xe_spv_arena_owns_record(const xe_spv_arena *arena, const void *record);
/* On-demand occupancy query: no per-record profiling counter in the allocator. */
size_t xe_spv_arena_used(const xe_spv_arena *arena);
#ifdef __cplusplus
}
#endif
#endif
