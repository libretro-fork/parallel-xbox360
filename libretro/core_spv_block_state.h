/* C89 debug-change tracking; validity flags share the block status byte.
 * Values need no initialization until their corresponding valid bit is set. */
#ifndef XE_CORE_SPV_BLOCK_STATE_H
#define XE_CORE_SPV_BLOCK_STATE_H
#include <stdint.h>
#include <retro_inline.h>
enum { XE_SPV_BLOCK_LOCATION_VALID = 1, XE_SPV_BLOCK_SCOPE_VALID = 2,
       XE_SPV_BLOCK_UNREACHABLE = 4 };
typedef struct xe_spv_block_debug {
   int line, column;
   uint32_t file, scope;
} xe_spv_block_debug;
static INLINE int xe_spv_block_source(xe_spv_block_debug *debug, uint8_t *flags,
      int line, int column, uint32_t file)
{
   if ((*flags & XE_SPV_BLOCK_LOCATION_VALID) && debug->line == line &&
       debug->column == column && debug->file == file) return 0;
   debug->line = line;
   debug->column = column;
   debug->file = file;
   *flags = (uint8_t)(*flags | XE_SPV_BLOCK_LOCATION_VALID);
   return 1;
}
static INLINE int xe_spv_block_scope(xe_spv_block_debug *debug, uint8_t *flags,
      uint32_t scope)
{
   if ((*flags & XE_SPV_BLOCK_SCOPE_VALID) && debug->scope == scope) return 0;
   debug->scope = scope;
   *flags = (uint8_t)(*flags | XE_SPV_BLOCK_SCOPE_VALID);
   return 1;
}
#endif
