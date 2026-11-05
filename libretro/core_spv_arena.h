/* Single-owner reusable storage. Retire explicit typed owners before terminal
 * cleanup of remaining records. Node storage is retained until close. */
#ifndef XE_CORE_SPV_ARENA_H
#define XE_CORE_SPV_ARENA_H
#include <stddef.h>
#include <stdint.h>
#include <retro_inline.h>
#include "core_spv_index.h"
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
typedef struct xe_spv_arena {
   struct xe_spv_arena_page *head, *tail, *cursor;
   size_t reserved_records;
   xe_spv_index index;
   int in_use;
} xe_spv_arena;
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
/* On-demand occupancy query: no per-record profiling counter in the allocator. */
size_t xe_spv_arena_used(const xe_spv_arena *arena);
#ifdef __cplusplus
}
#endif
#endif
