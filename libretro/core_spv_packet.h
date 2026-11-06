/* Mutable encoded instruction. Words include the wire prefix from construction.
 * Opcode/type/result are immutable until init/free; only operands may be patched.
 * Inline prefix bytes remain queryable after promotion to pooled words; no extra
 * cache or copy is needed. Views expire on growth/free. No shallow copies. */
#ifndef XE_CORE_SPV_PACKET_H
#define XE_CORE_SPV_PACKET_H
#include <limits.h>
#include <assert.h>
#include <retro_inline.h>
#include "core_spv_words.h"
#include <string.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xe_spv_packet {
   uint32_t *heap;
   uint32_t count;
   /* Same byte-12 arena tag as the existing typed instruction records. */
   uint32_t arena_offset;
   uint32_t capacity;
   uint32_t local_words[7];
   /* Low seven bits: inline operand ID flags. Bits 8/9: type/result prefix.
    * Bit 10: pooled payload. Bits 12/13: prefix word count. Upper 16: invalid
    * opcode bits, checked at output. */
   uint32_t flags;
} xe_spv_packet;
/* Borrow a workspace. Old is supplied only for pooled storage. A successful
 * callback may extend it in place, but must never free it. Spans are 8-byte
 * aligned/padded; their owner outlives every packet that borrows them. */
typedef void *(*xe_spv_packet_realloc_fn)(void *, void *, size_t, size_t);
enum { XE_SPV_PACKET_TYPE = 256, XE_SPV_PACKET_RESULT = 512,
       XE_SPV_PACKET_POOLED = 1024 };
static INLINE size_t xe_spv_packet_prefix(const xe_spv_packet *packet) {
   return (packet->flags >> 12) & 3u;
}
static INLINE uint32_t *xe_spv_packet_words(xe_spv_packet *packet) {
   return packet->heap ? packet->heap : packet->local_words;
}
static INLINE const uint32_t *xe_spv_packet_const_words(const xe_spv_packet *packet) {
   return packet->heap ? packet->heap : packet->local_words;
}
static INLINE uint32_t *xe_spv_packet_operands(xe_spv_packet *packet) {
   return xe_spv_packet_words(packet) + xe_spv_packet_prefix(packet);
}
static INLINE const uint32_t *xe_spv_packet_const_operands(const xe_spv_packet *packet) {
   return xe_spv_packet_const_words(packet) + xe_spv_packet_prefix(packet);
}
static INLINE uint32_t xe_spv_packet_opcode(const xe_spv_packet *packet) {
   return (packet->local_words[0] & 65535u) |
         (packet->flags & UINT32_C(0xffff0000));
}
static INLINE uint32_t xe_spv_packet_type(const xe_spv_packet *packet) {
   return packet->flags & XE_SPV_PACKET_TYPE ? packet->local_words[1] : 0;
}
static INLINE uint32_t xe_spv_packet_result(const xe_spv_packet *packet) {
   return packet->flags & XE_SPV_PACKET_RESULT ? packet->local_words[
         1 + ((packet->flags & XE_SPV_PACKET_TYPE) != 0)] : 0;
}
/* Constant-cache records always have both type and result. Compare their raw
 * prefix once, rather than rediscovering the layout through graph accessors. */
static INLINE int xe_spv_packet_constant_equal(const xe_spv_packet *packet,
      uint32_t opcode, uint32_t type, const uint32_t *operands, size_t count) {
   const uint32_t *words;
   if ((packet->flags & UINT32_C(0xffff0300)) !=
         (XE_SPV_PACKET_TYPE | XE_SPV_PACKET_RESULT) || packet->count != count) return 0;
   words = xe_spv_packet_const_words(packet);
   if ((words[0] & 65535u) != opcode || words[1] != type) return 0;
   /* Scalars and the common pair composite need no out-of-line byte comparator.
    * Compare integer representations, preserving NaNs and signed zero exactly. */
   if (count <= 2) return (!count || words[3] == operands[0]) &&
         (count < 2 || words[4] == operands[1]);
   return !memcmp(words + 3, operands, count * sizeof(uint32_t));
}
static INLINE uint32_t xe_spv_packet_typed_result(const xe_spv_packet *packet) {
   assert((packet->flags & (XE_SPV_PACKET_TYPE | XE_SPV_PACKET_RESULT)) ==
         (XE_SPV_PACKET_TYPE | XE_SPV_PACKET_RESULT));
   return packet->local_words[2];
}
static INLINE int xe_spv_packet_constant_repeat_equal(const xe_spv_packet *packet,
      uint32_t opcode, uint32_t type, uint32_t word, size_t count) {
   const uint32_t *words;
   size_t i;
   if ((packet->flags & UINT32_C(0xffff0300)) !=
         (XE_SPV_PACKET_TYPE | XE_SPV_PACKET_RESULT) || packet->count != count) return 0;
   words = xe_spv_packet_const_words(packet);
   if ((words[0] & 65535u) != opcode || words[1] != type) return 0;
   for (i = 0; i < count; ++i) if (words[3 + i] != word) return 0;
   return 1;
}
static INLINE int xe_spv_packet_valid(const xe_spv_packet *packet) {
   return !(packet->flags & UINT32_C(0xffff0000)) &&
         packet->count <= 65535u - xe_spv_packet_prefix(packet);
}
static INLINE xe_spv_word_view xe_spv_packet_view(const xe_spv_packet *packet) {
   return xe_spv_words_view(xe_spv_packet_const_words(packet),
         xe_spv_packet_prefix(packet) + packet->count);
}
static INLINE void xe_spv_packet_init(xe_spv_packet *packet,
      uint32_t opcode, uint32_t type, uint32_t result) {
   size_t prefix = 1;
   packet->heap = NULL;
   packet->count = 0;
   packet->arena_offset = 0;
   packet->flags = (opcode & UINT32_C(0xffff0000)) |
         (type ? XE_SPV_PACKET_TYPE : 0) | (result ? XE_SPV_PACKET_RESULT : 0);
   if (type) packet->local_words[prefix++] = type;
   if (result) packet->local_words[prefix++] = result;
   packet->flags |= (uint32_t)prefix << 12;
   packet->local_words[0] = ((uint32_t)prefix << 16) | (opcode & 65535u);
   packet->capacity = (uint32_t)(7 - prefix);
}
static INLINE int xe_spv_packet_is_id(const xe_spv_packet *packet, uint32_t index) {
   const uint8_t *flags;
   assert(index < packet->count);
   if (!packet->heap) return (packet->flags >> index) & 1;
   flags = (const uint8_t *)(packet->heap + xe_spv_packet_prefix(packet) + packet->capacity);
   return (flags[index >> 3] >> (index & 7)) & 1;
}
static INLINE void xe_spv_packet_append_reserved(xe_spv_packet *packet, uint32_t word, int is_id) {
   uint8_t *flags;
   uint32_t *words = xe_spv_packet_words(packet);
   size_t prefix = xe_spv_packet_prefix(packet);
   assert(packet->count < packet->capacity);
   words[prefix + packet->count] = word;
   if (packet->heap) {
      flags = (uint8_t *)(words + prefix + packet->capacity);
      if (!(packet->count & 7)) flags[packet->count >> 3] = 0;
      if (is_id) flags[packet->count >> 3] |= (uint8_t)(1u << (packet->count & 7));
   } else {
      packet->flags &= ~(1u << packet->count);
      if (is_id) packet->flags |= 1u << packet->count;
   }
   ++packet->count;
   words[0] += UINT32_C(0x10000);
}
static INLINE size_t xe_spv_packet_growth(const xe_spv_packet *packet) {
   if (packet->capacity >= INT_MAX) return 0;
   return packet->capacity > INT_MAX / 2 ? INT_MAX : packet->capacity * 2;
}
static INLINE size_t xe_spv_packet_string_capacity(const xe_spv_packet *packet, size_t bytes) {
   size_t added = bytes / 4 + 1;
   if (packet->count > INT_MAX || added > (size_t)INT_MAX - packet->count) return 0;
   return packet->count + added;
}
static INLINE size_t xe_spv_packet_source_capacity(const xe_spv_packet *packet, size_t count) {
   if (packet->count > INT_MAX || count > (size_t)INT_MAX - packet->count) return 0;
   return packet->count + count;
}
/* Caller reserves once; no source buffer is retained or staged. */
static INLINE void xe_spv_packet_source_ids_reserved(xe_spv_packet *packet,
      xe_spv_word_source source, size_t count) {
   size_t i;
   assert(count <= source.count && count <= packet->capacity - packet->count);
   if (source.repeated) {
      for (i = 0; i < count; ++i) {
         assert(*source.words);
         xe_spv_packet_append_reserved(packet, *source.words, 1);
      }
   } else {
      for (i = 0; i < count; ++i) {
         assert(source.words[i]);
         xe_spv_packet_append_reserved(packet, source.words[i], 1);
      }
   }
}
void xe_spv_packet_free(xe_spv_packet *packet);
int xe_spv_packet_reserve_with(xe_spv_packet *packet, size_t capacity,
      xe_spv_packet_realloc_fn resize_storage, void *context);
/* Caller reserves once using its matching arena callback before encoding. */
int xe_spv_packet_string_reserved(xe_spv_packet *packet, const char *text, size_t bytes);
#ifdef __cplusplus
}
#endif
#endif
