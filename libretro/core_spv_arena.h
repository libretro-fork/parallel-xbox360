/* Single-owner reusable storage. Retire explicit typed owners before terminal
 * cleanup of remaining records. Node storage is retained until close. */
#ifndef XE_CORE_SPV_ARENA_H
#define XE_CORE_SPV_ARENA_H
#include <stddef.h>
#include <stdint.h>
#include <retro_inline.h>
#include "core_spv_index.h"
#include "core_spv_constant_cache.h"
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
