/* Bounded, allocation-free state I/O. MSVC C89 compatible. */
#ifndef XE_CORE_STATE_IO_H
#define XE_CORE_STATE_IO_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <retro_inline.h>

typedef struct xe_state_cursor {
   unsigned char *data;
   size_t size, offset;
   int measuring;
} xe_state_cursor;
typedef struct xe_state_text {
   const char *data;
   size_t size;
} xe_state_text;

static INLINE int xe_state_range(const xe_state_cursor *cursor, size_t count)
{
   return cursor->offset <= cursor->size && count <= cursor->size - cursor->offset;
}
static INLINE int xe_state_seek(xe_state_cursor *cursor, size_t offset)
{
   if (offset > cursor->size) return 0;
   cursor->offset = offset;
   return 1;
}
static INLINE int xe_state_advance(xe_state_cursor *cursor, size_t count)
{
   if (!xe_state_range(cursor, count)) return 0;
   cursor->offset += count;
   return 1;
}
static INLINE int xe_state_read(xe_state_cursor *cursor, void *output, size_t count)
{
   if (cursor->measuring || !xe_state_range(cursor, count) ||
         (count && (!cursor->data || !output))) return 0;
   if (count) memcpy(output, cursor->data + cursor->offset, count);
   cursor->offset += count;
   return 1;
}
static INLINE int xe_state_write(xe_state_cursor *cursor, const void *input, size_t count)
{
   if (!xe_state_range(cursor, count)) return 0;
   if (!cursor->measuring && count) {
      if (!cursor->data || !input) return 0;
      memcpy(cursor->data + cursor->offset, input, count);
   }
   cursor->offset += count;
   return 1;
}
static INLINE int xe_state_view(xe_state_cursor *cursor, const void **output, size_t count)
{
   if (!output || cursor->measuring || !xe_state_range(cursor, count) ||
         (count && !cursor->data)) return 0;
   *output = cursor->data ? cursor->data + cursor->offset : NULL;
   cursor->offset += count;
   return 1;
}
static INLINE int xe_state_patch(xe_state_cursor *cursor, size_t offset,
      const void *input, size_t count)
{
   /* Patching cannot reach unwritten or unvalidated bytes. */
   if (offset > cursor->offset || count > cursor->offset - offset) return 0;
   if (!cursor->measuring && count) {
      if (!cursor->data || !input) return 0;
      memcpy(cursor->data + offset, input, count);
   }
   return 1;
}
/* UTF-8 bytes borrow the input buffer; no temporary string or allocation. */
static INLINE int xe_state_read_text(xe_state_cursor *cursor, xe_state_text *text)
{
   uint32_t length;
   if (!text || cursor->measuring || !cursor->data ||
         !xe_state_range(cursor, sizeof(length))) return 0;
   memcpy(&length, cursor->data + cursor->offset, sizeof(length));
   if (length > cursor->size - cursor->offset - sizeof(length)) return 0;
   text->data = (const char *)cursor->data + cursor->offset + sizeof(length);
   text->size = length;
   cursor->offset += sizeof(length) + length;
   return 1;
}
static INLINE int xe_state_write_text(xe_state_cursor *cursor, const char *data, size_t size)
{
   uint32_t length;
   if (size > UINT32_MAX || !xe_state_range(cursor, sizeof(length)) ||
         size > cursor->size - cursor->offset - sizeof(length) ||
         (!cursor->measuring && size && !data)) return 0;
   length = (uint32_t)size;
   return xe_state_write(cursor, &length, sizeof(length)) &&
         xe_state_write(cursor, data, size);
}
#endif
