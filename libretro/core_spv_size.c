#include "core_spv_size.h"
void xe_spv_count_source(xe_spv_count *out, int has_file, size_t text_bytes)
{
   size_t full, tail, words = 3, tail_words;
   size_t limit = (size_t)-1 / sizeof(uint32_t);
   const size_t continued_words = XE_SPV_SOURCE_CHUNK_BYTES / 4 + 2;
   if (!has_file || !text_bytes) {
      xe_spv_count_words(out, 3 + (has_file != 0));
      return;
   }
   full = text_bytes / XE_SPV_SOURCE_CHUNK_BYTES;
   tail = text_bytes % XE_SPV_SOURCE_CHUNK_BYTES;
   /* All chunks have one opcode word; the first has three extra operands. */
   if (full > (limit - words) / continued_words) { out->failed = 1; return; }
   words += full * continued_words;
   tail_words = tail ? tail / 4 + 2 : 0;
   if (tail_words > limit - words) { out->failed = 1; return; }
   xe_spv_count_words(out, words + tail_words);
}
