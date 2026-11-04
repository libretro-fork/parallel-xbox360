#ifndef XE_CORE_XMA_PACKET_H
#define XE_CORE_XMA_PACKET_H
#include <stdint.h>
#include <retro_inline.h>
enum { XE_XMA_FRAME_MAX_BITS = 32767, XE_XMA_PACKET_CAPACITY = 4098 };
typedef struct xe_xma_packet_span {
   uint32_t size;
   uint8_t header;
} xe_xma_packet_span;
static INLINE int xe_xma_packet_layout(uint32_t bits, uint32_t padding,
      uint32_t capacity, xe_xma_packet_span *packet)
{
   uint32_t size, trailing;
   if (!packet || bits < 15 || bits > XE_XMA_FRAME_MAX_BITS || padding > 7)
      return 0;
   size = 1 + (padding + bits + 7) / 8;
   if (size > capacity) return 0;
   trailing = (size - 1) * 8 - padding - bits;
   packet->size = size;
   packet->header = (uint8_t)((padding << 5) | (trailing << 2));
   return 1;
}
#endif
