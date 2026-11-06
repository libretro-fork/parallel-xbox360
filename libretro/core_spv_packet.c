#include "core_spv_packet.h"
#include "core_spv_string.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#ifndef XE_SPV_ALLOC
#define XE_SPV_ALLOC malloc
#define XE_SPV_FREE free
#endif
/* Only pooled spans have guaranteed rounded padding beyond their flags. */
#if defined(__SANITIZE_ADDRESS__)
#define XE_SPV_PACKET_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define XE_SPV_PACKET_ASAN 1
#endif
#endif
#ifdef XE_SPV_PACKET_ASAN
#include <sanitizer/asan_interface.h>
#endif
static size_t packet_bytes(const xe_spv_packet *packet) {
   return (xe_spv_packet_prefix(packet) + packet->capacity) * sizeof(uint32_t) +
         (packet->capacity + 7u) / 8u;
}
static void packet_release(xe_spv_packet *packet) {
   if (!packet->heap) return;
   if (!(packet->flags & XE_SPV_PACKET_POOLED)) XE_SPV_FREE(packet->heap);
#ifdef XE_SPV_PACKET_ASAN
   else __asan_poison_memory_region(packet->heap, (packet_bytes(packet) + 7) & ~(size_t)7);
#endif
}
void xe_spv_packet_free(xe_spv_packet *packet) {
   uint32_t arena_offset = packet->arena_offset;
   packet_release(packet);
   xe_spv_packet_init(packet, 0, 0, 0);
   packet->arena_offset = arena_offset;
}
int xe_spv_packet_reserve_with(xe_spv_packet *packet, size_t capacity,
      xe_spv_packet_realloc_fn resize_storage, void *context) {
   size_t prefix = xe_spv_packet_prefix(packet), flag_size, bytes, old_bytes = 0;
   uint32_t *words;
   uint8_t *flags;
   uint32_t i;
   if (capacity <= packet->capacity) return 1;
   if (capacity > INT_MAX) return 0;
   flag_size = (capacity + 7) / 8;
   if (capacity > (SIZE_MAX - flag_size) / sizeof(uint32_t) - prefix) return 0;
   bytes = (prefix + capacity) * sizeof(uint32_t) + flag_size;
   if (packet->heap && (packet->flags & XE_SPV_PACKET_POOLED)) old_bytes = packet_bytes(packet);
   words = resize_storage ? (uint32_t *)resize_storage(context,
         old_bytes ? packet->heap : NULL, old_bytes, bytes) : (uint32_t *)XE_SPV_ALLOC(bytes);
   if (!words) return 0;
   flags = (uint8_t *)(words + prefix + capacity);
   if (words != packet->heap)
      memcpy(words, xe_spv_packet_const_words(packet), (prefix + packet->count) * sizeof(uint32_t));
   if (packet->count) {
      if (packet->heap) memmove(flags,
            (const uint8_t *)(packet->heap + prefix + packet->capacity), (packet->count + 7) / 8);
      else {
         flags[0] = 0;
         for (i = 0; i < packet->count; ++i)
            if ((packet->flags >> i) & 1) flags[0] |= (uint8_t)(1u << i);
      }
   }
   if (words != packet->heap) packet_release(packet);
   packet->heap = words;
   packet->capacity = (uint32_t)capacity;
   packet->flags &= ~XE_SPV_PACKET_POOLED;
   if (resize_storage) packet->flags |= XE_SPV_PACKET_POOLED;
   return 1;
}
int xe_spv_packet_string_reserved(xe_spv_packet *packet, const char *text, size_t bytes) {
   size_t added = bytes / 4 + 1, first = packet->count, end, byte;
   uint32_t *words = xe_spv_packet_words(packet);
   uint8_t *flags;
   if ((bytes && !text) || first > packet->capacity || added > packet->capacity - first) return 0;
   xe_spv_string_encode(words + xe_spv_packet_prefix(packet) + first, text, bytes);
   if (packet->heap) {
      flags = (uint8_t *)(words + xe_spv_packet_prefix(packet) + packet->capacity);
      byte = first / 8;
      end = (first + added + 7) / 8;
      if (first & 7) flags[byte] &= (uint8_t)((1u << (first & 7)) - 1);
      else flags[byte] = 0;
      if (end > byte + 1) memset(flags + byte + 1, 0, end - byte - 1);
   } else {
      uint32_t mask = ((1u << added) - 1u) << first;
      packet->flags &= ~mask;
   }
   packet->count = (uint32_t)(first + added);
   words[0] += (uint32_t)added << 16;
   return 1;
}
