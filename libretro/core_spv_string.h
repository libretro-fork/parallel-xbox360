/* Encode a borrowed byte span directly into its final owned SPIR-V words.
 * Caller reserves bytes / 4 + 1 words; input must not alias the output. */
#ifndef XE_CORE_SPV_STRING_H
#define XE_CORE_SPV_STRING_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
static uint32_t xe_spv_string_host_word(uint32_t word)
{
   const uint32_t one = 1;
   /* Character access is alias-safe. Compilers fold this on each target. */
   if (!*(const unsigned char *)&one)
      word = (word >> 24) | ((word >> 8) & UINT32_C(0x0000FF00)) |
            ((word << 8) & UINT32_C(0x00FF0000)) | (word << 24);
   return word;
}
static void xe_spv_string_encode(uint32_t *words, const char *text, size_t bytes)
{
   size_t full = bytes / 4, i;
   uint32_t word;
   /* Fixed-width memcpy permits unaligned byte input. Typed stores also retain
    * uint32_t effective type in malloc-backed operand/output storage. */
   for (i = 0; i < full; ++i) {
      memcpy(&word, text + i * 4, sizeof(word));
      words[i] = xe_spv_string_host_word(word);
   }
   word = 0;
   if (bytes & 3) memcpy(&word, text + full * 4, bytes & 3);
   words[full] = xe_spv_string_host_word(word);
}
#endif
