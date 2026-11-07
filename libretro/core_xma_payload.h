/* Borrow packet payloads; gather only the selected raw frame. MSVC C89. */
#ifndef XE_CORE_XMA_PAYLOAD_H
#define XE_CORE_XMA_PAYLOAD_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "libretro/core_xma_packet.h"
typedef struct xe_xma_payload_view {
   const uint8_t *first, *second;
   uint32_t first_size, second_size;
} xe_xma_payload_view;
static INLINE int xe_xma_payload_range(const xe_xma_payload_view *view,
      uint32_t offset, uint32_t count)
{
   uint32_t total;
   if (!view || view->first_size > XE_XMA_PACKET_CAPACITY ||
         view->second_size > XE_XMA_PACKET_CAPACITY ||
         (view->first_size && !view->first) ||
         (view->second_size && !view->second)) return 0;
   total = view->first_size + view->second_size;
   return offset <= total && count <= total - offset;
}
static INLINE uint8_t xe_xma_payload_byte(const xe_xma_payload_view *view,
      uint32_t offset)
{
   return offset < view->first_size ? view->first[offset] :
         view->second[offset - view->first_size];
}
/* Only the 15-bit length header is inspected, including a split header. */
static INLINE int xe_xma_payload_length(const xe_xma_payload_view *view,
      uint32_t bit_offset, uint32_t *length)
{
   uint32_t offset, padding, bytes, word, i;
   if (!length) return 0;
   offset = bit_offset / 8; padding = bit_offset & 7;
   bytes = (padding + 15 + 7) / 8;
   if (!xe_xma_payload_range(view, offset, bytes)) return 0;
   word = 0;
   for (i = 0; i < bytes; ++i)
      word = (word << 8) | xe_xma_payload_byte(view, offset + i);
   *length = (word >> (bytes * 8 - padding - 15)) & 32767;
   return 1;
}
/* Source spans and destination must not overlap. Validate before any write.
 * The caller retains its existing owned codec packet and clears codec padding. */
static INLINE int xe_xma_payload_packet(const xe_xma_payload_view *view,
      uint32_t bit_offset, uint32_t bits, uint8_t *output, uint32_t capacity,
      xe_xma_packet_span *packet)
{
   xe_xma_packet_span plan;
   uint32_t offset, bytes, first, padding, trailing;
   if (!output || !packet) return 0;
   offset = bit_offset / 8; padding = bit_offset & 7;
   if (!xe_xma_packet_layout(bits, padding, capacity, &plan)) return 0;
   bytes = plan.size - 1;
   if (!xe_xma_payload_range(view, offset, bytes)) return 0;
   first = offset < view->first_size ? view->first_size - offset : 0;
   if (first > bytes) first = bytes;
   if (first) memcpy(output + 1, view->first + offset, first);
   if (bytes > first) memcpy(output + 1 + first,
         view->second + (offset + first - view->first_size), bytes - first);
   output[0] = plan.header;
   output[1] &= (uint8_t)(255u >> padding);
   trailing = (plan.header >> 2) & 7;
   output[plan.size - 1] &= (uint8_t)(255u << trailing);
   *packet = plan;
   return 1;
}
#endif
