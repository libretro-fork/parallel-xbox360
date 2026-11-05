/* Checked C89 serialization into reusable storage, without a second module
 * buffer. IR operand words are borrowed until the instruction append returns. */
#include "core_spv_binary.h"
#include "core_spv_string.h"
#include <stdlib.h>
#include <string.h>

#ifndef XE_SPV_BINARY_REALLOC
#define XE_SPV_BINARY_REALLOC realloc
#endif
#ifndef XE_SPV_BINARY_FREE
#define XE_SPV_BINARY_FREE free
#endif

int xe_spv_binary_reserve_exact(xe_spv_binary *out, size_t words)
{
   uint32_t *allocation;
   if (out->failed) return 0;
   if (words <= out->capacity) return 1;
   if (words > (size_t)-1 / sizeof(uint32_t)) { out->failed = 1; return 0; }
   allocation = (uint32_t *)XE_SPV_BINARY_REALLOC(out->words,
         words * sizeof(uint32_t));
   if (!allocation) { out->failed = 1; return 0; }
   out->words = allocation;
   out->capacity = words;
   return 1;
}

int xe_spv_binary_reserve(xe_spv_binary *out, size_t words)
{
   size_t capacity, limit = (size_t)-1 / sizeof(uint32_t);
   if (out->failed) return 0;
   if (words <= out->capacity) return 1;
   if (words > limit) { out->failed = 1; return 0; }
   capacity = out->capacity ? out->capacity : 16;
   while (capacity < words) {
      if (capacity > limit / 2) { capacity = words; break; }
      capacity *= 2;
   }
   return xe_spv_binary_reserve_exact(out, capacity);
}

void xe_spv_binary_take(xe_spv_binary *out, xe_spv_binary *source)
{
   if (out == source) return;
   xe_spv_binary_close(out);
   *out = *source;
   memset(source, 0, sizeof(*source));
}

void xe_spv_binary_reset(xe_spv_binary *out)
{
   out->count = 0;
   out->failed = 0;
}

void xe_spv_binary_close(xe_spv_binary *out)
{
   XE_SPV_BINARY_FREE(out->words);
   memset(out, 0, sizeof(*out));
}

void xe_spv_binary_word(xe_spv_binary *out, uint32_t word)
{
   if (out->count == (size_t)-1 ||
       !xe_spv_binary_reserve(out, out->count + 1)) {
      out->failed = 1;
      return;
   }
   out->words[out->count++] = word;
}

void xe_spv_binary_instruction(xe_spv_binary *out, uint32_t opcode,
      uint32_t type, uint32_t result, const uint32_t *operands, size_t count)
{
   size_t header, words;
   uint32_t *dest;
   if (out->failed) return;
   header = 1 + (type != 0) + (result != 0);
   if (opcode > 65535 || count > 65535 - header ||
       (count && !operands)) { out->failed = 1; return; }
   words = header + count;
   if (out->count > (size_t)-1 - words ||
       !xe_spv_binary_reserve(out, out->count + words)) {
      out->failed = 1;
      return;
   }
   dest = out->words + out->count;
   *dest++ = ((uint32_t)words << 16) | opcode;
   if (type) *dest++ = type;
   if (result) *dest++ = result;
   if (count) memcpy(dest, operands, count * sizeof(uint32_t));
   out->count += words;
}

void xe_spv_binary_append(xe_spv_binary *out, const uint32_t *words, size_t count)
{
   if (out->failed || !count) return;
   if (!words || count > (size_t)-1 / sizeof(uint32_t) ||
       out->count > (size_t)-1 - count ||
       !xe_spv_binary_reserve(out, out->count + count)) {
      out->failed = 1;
      return;
   }
   memcpy(out->words + out->count, words, count * sizeof(uint32_t));
   out->count += count;
}

void xe_spv_binary_string_span(xe_spv_binary *out, uint32_t opcode,
      const uint32_t *prefix, size_t prefix_count, const char *text, size_t bytes)
{
   size_t words, string_words = bytes / 4 + 1;
   uint32_t *dest;
   if (out->failed) return;
   if ((bytes && !text) || (prefix_count && !prefix) || opcode > 65535 ||
       prefix_count > 65533 || string_words > 65534 - prefix_count) {
      out->failed = 1;
      return;
   }
   words = 1 + prefix_count + string_words;
   if (out->count > (size_t)-1 - words ||
       !xe_spv_binary_reserve(out, out->count + words)) {
      out->failed = 1;
      return;
   }
   dest = out->words + out->count;
   *dest++ = ((uint32_t)words << 16) | opcode;
   if (prefix_count) memcpy(dest, prefix, prefix_count * sizeof(*prefix));
   xe_spv_string_encode(dest + prefix_count, text, bytes);
   out->count += words;
}
/* The caller may mutate or release text immediately after this call. */
void xe_spv_binary_string(xe_spv_binary *out, uint32_t opcode, const char *text)
{
   if (out->failed) return;
   if (!text) { out->failed = 1; return; }
   xe_spv_binary_string_span(out, opcode, NULL, 0, text, strlen(text));
}
