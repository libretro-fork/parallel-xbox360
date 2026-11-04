#include "core_file_blocks.h"
#include "core_file_span.h"
#include <stdint.h>
#include <stdlib.h>
#ifndef XE_BLOCK_ALLOC
#define XE_BLOCK_ALLOC malloc
#define XE_BLOCK_FREE free
#endif
void xe_file_blocks_free(xe_file_blocks *blocks) {
   XE_BLOCK_FREE(blocks->data);
   blocks->data = NULL;
   blocks->count = blocks->capacity = 0;
}
int xe_file_blocks_append(xe_file_blocks *blocks, size_t file, size_t offset, size_t length) {
   xe_file_block *last, *data;
   size_t capacity, limit = SIZE_MAX / sizeof(*data);
   if (!length) return 1;
   if (length > SIZE_MAX - offset) return 0;
   if (blocks->count) {
      last = &blocks->data[blocks->count - 1];
      if (last->file == file && last->length <= SIZE_MAX - last->offset &&
            last->offset + last->length == offset) {
         if (length > SIZE_MAX - last->length) return 0;
         last->length += length;
         return 1;
      }
   }
   if (blocks->count == blocks->capacity) {
      if (blocks->count == limit) return 0;
      capacity = blocks->capacity ? (blocks->capacity > limit / 2 ? limit : blocks->capacity * 2) : 1;
      data = (xe_file_block *)XE_BLOCK_ALLOC(capacity * sizeof(*data));
      if (!data) return -1;
      if (blocks->count) memcpy(data, blocks->data, blocks->count * sizeof(*data));
      XE_BLOCK_FREE(blocks->data);
      blocks->data = data;
      blocks->capacity = capacity;
   }
   last = &blocks->data[blocks->count++];
   last->file = file; last->offset = offset; last->length = length;
   return 1;
}
typedef struct xe_block_transfer {
   xe_file_block_validate validate;
   xe_file_block_read read;
   void *opaque;
   unsigned char *destination;
   size_t *written;
   int execute;
} xe_block_transfer;
static int deliver(xe_block_transfer *transfer, const xe_file_block *run) {
   size_t got;
   if (!transfer->execute)
      return !transfer->validate || transfer->validate(transfer->opaque, run->file, run->offset, run->length);
   got = transfer->read(transfer->opaque, run->file, run->offset,
         transfer->destination + *transfer->written, run->length);
   if (got > run->length) return 0;
   *transfer->written += got;
   return got == run->length;
}
static int walk(const xe_file_block *blocks, size_t count, size_t index,
      size_t within, size_t remaining, xe_block_transfer *transfer) {
   xe_file_block run = {0, 0, 0};
   const xe_file_block *block;
   size_t take, physical;
   while (remaining) {
      if (index == count) return 0;
      block = &blocks[index++];
      if (within > block->length) return 0;
      take = block->length - within;
      if (take > remaining) take = remaining;
      if (!take) { within = 0; continue; }
      if (!xe_file_span_range(SIZE_MAX, block->offset, within, take, &physical)) return 0;
      if (run.length && (run.file != block->file || run.offset + run.length != physical)) {
         if (!deliver(transfer, &run)) return 0;
         run.length = 0;
      }
      if (!run.length) { run.file = block->file; run.offset = physical; }
      if (take > SIZE_MAX - run.length) return 0;
      run.length += take;
      remaining -= take;
      within = 0;
   }
   return !run.length || deliver(transfer, &run);
}
int xe_file_blocks_read(const xe_file_block *blocks, size_t count, size_t logical_size,
      size_t offset, void *destination, size_t length, xe_file_block_validate validate,
      xe_file_block_read read, void *opaque, size_t *bytes_read) {
   xe_block_transfer transfer;
   size_t index, logical = 0, within, wanted;
   if (!bytes_read) return 0;
   *bytes_read = 0;
   if (offset >= logical_size) return 0;
   wanted = logical_size - offset;
   if (wanted > length) wanted = length;
   if (!wanted) return 1;
   if (!blocks || count > SIZE_MAX / sizeof(*blocks) || !destination || !read) return 0;
   for (index = 0; index < count; ++index) {
      if (blocks[index].length > SIZE_MAX - logical) return 0;
      if (offset < logical + blocks[index].length) break;
      logical += blocks[index].length;
   }
   if (index == count) return 0;
   within = offset - logical;
   transfer.validate = validate; transfer.read = read; transfer.opaque = opaque;
   transfer.destination = (unsigned char *)destination; transfer.written = bytes_read;
   transfer.execute = 0;
   if (!walk(blocks, count, index, within, wanted, &transfer)) return 0;
   transfer.execute = 1;
   return walk(blocks, count, index, within, wanted, &transfer);
}
