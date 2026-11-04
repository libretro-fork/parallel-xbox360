#include "libretro/core_xma_packet.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) abort(); } while (0)
int main(void)
{
   uint32_t bits, padding, payload_bits, trailing;
   xe_xma_packet_span packet;
   for (bits = 15; bits <= XE_XMA_FRAME_MAX_BITS; ++bits)
      for (padding = 0; padding < 8; ++padding) {
         CHECK(xe_xma_packet_layout(bits, padding, XE_XMA_PACKET_CAPACITY, &packet));
         CHECK((uint32_t)(packet.header >> 5) == padding);
         trailing = (packet.header >> 2) & 7;
         payload_bits = (packet.size - 1) * 8;
         CHECK(payload_bits == bits + padding + trailing);
         CHECK(!(packet.header & 3));
         CHECK(packet.size <= XE_XMA_PACKET_CAPACITY);
      }
   CHECK(xe_xma_packet_layout(32766, 7, XE_XMA_PACKET_CAPACITY, &packet));
   CHECK(packet.size == 4098);
   CHECK(!xe_xma_packet_layout(32766, 7, 4097, &packet));
   packet.size = 0xDEAD;
   packet.header = 0xA5;
   CHECK(!xe_xma_packet_layout(14, 0, XE_XMA_PACKET_CAPACITY, &packet));
   CHECK(!xe_xma_packet_layout(32768, 0, XE_XMA_PACKET_CAPACITY, &packet));
   CHECK(!xe_xma_packet_layout(15, 8, XE_XMA_PACKET_CAPACITY, &packet));
   CHECK(!xe_xma_packet_layout(15, 0, 0, &packet));
   CHECK(packet.size == 0xDEAD && packet.header == 0xA5);
   puts("PASS: every XMA frame length/bit alignment fits padded packet backing; invalid layouts leave output untouched");
   return 0;
}
