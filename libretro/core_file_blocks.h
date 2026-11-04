/* Owned cold-built metadata; immutable while a transfer is in progress. */
#ifndef XE_CORE_FILE_BLOCKS_H
#define XE_CORE_FILE_BLOCKS_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xe_file_block {
   size_t file, offset, length;
} xe_file_block;
typedef struct xe_file_blocks {
   xe_file_block *data;
   size_t count, capacity;
} xe_file_blocks;
/* Zero-initialize before use; no shallow copies. Append coalesces adjacent
 * ranges in the same file. Returns 1, 0 invalid, or -1 allocation failure. */
int xe_file_blocks_append(xe_file_blocks *blocks, size_t file, size_t offset, size_t length);
void xe_file_blocks_free(xe_file_blocks *blocks);
typedef int (*xe_file_block_validate)(void *, size_t file, size_t offset, size_t length);
typedef size_t (*xe_file_block_read)(void *, size_t file, size_t offset, void *destination, size_t length);
/* Preflight all requested ranges before touching destination. Extents and
 * backing ownership stay stable through both passes; destination must not
 * alias metadata or immutable backing. Short I/O returns 0 with prefix bytes.
 * This transfer allocates no storage and uses no gather/staging buffer. */
int xe_file_blocks_read(const xe_file_block *blocks, size_t count, size_t logical_size,
      size_t offset, void *destination, size_t length, xe_file_block_validate validate,
      xe_file_block_read read, void *opaque, size_t *bytes_read);
#ifdef __cplusplus
}
#endif
#endif
