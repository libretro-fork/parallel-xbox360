/* Allocation-free wire sizing; never reads operand payloads. */
#ifndef XE_CORE_SPV_SIZE_H
#define XE_CORE_SPV_SIZE_H
#include <stddef.h>
#include <stdint.h>
#include <retro_inline.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xe_spv_count {
   size_t count;
   int failed;
} xe_spv_count;
enum { XE_SPV_SOURCE_CHUNK_BYTES = 4 * (65535 - 4) - 1 };
static INLINE void xe_spv_count_words(xe_spv_count *out, size_t count)
{
   size_t limit = (size_t)-1 / sizeof(uint32_t);
   if (out->failed) return;
   if (out->count > limit || count > limit - out->count) {
      out->failed = 1;
      return;
   }
   out->count += count;
}
static INLINE void xe_spv_count_instruction(xe_spv_count *out, uint32_t opcode,
      uint32_t type, uint32_t result, size_t count)
{
   size_t header = 1 + (type != 0) + (result != 0);
   if (out->failed) return;
   if (opcode > 65535 || count > 65535 - header) {
      out->failed = 1;
      return;
   }
   xe_spv_count_words(out, header + count);
}
void xe_spv_count_source(xe_spv_count *out, int has_file, size_t text_bytes);
#ifdef __cplusplus
}
#endif
#endif
