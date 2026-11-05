#include "../../libretro/core_spv_packet.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef char packet_tag_at_byte_12[(offsetof(xe_spv_packet, arena_offset) == 12) ? 1 : -1];
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL packet line %u\n",(unsigned)__LINE__); return 1; } } while (0)
static unsigned allocations, live, fail_next;
static void *packet_alloc(size_t bytes) {
   void *value;
   ++allocations;
   if (fail_next) { fail_next = 0; return NULL; }
   value = malloc(bytes);
   if (value) { memset(value, 0xad, bytes); ++live; }
   return value;
}
static void packet_free(void *value) { if (value) { --live; free(value); } }
#define XE_SPV_ALLOC packet_alloc
#define XE_SPV_FREE packet_free
#include "../../libretro/core_spv_operands.c"
static int layout(unsigned type, unsigned result) {
   xe_spv_packet packet;
   xe_spv_word_view view;
   size_t prefix = 1 + (type != 0) + (result != 0), i;
   unsigned before = allocations;
   const uint32_t *stable;
   xe_spv_packet_init(&packet, 128, type, result);
   packet.arena_offset = 128;
   CHECK(xe_spv_packet_type(&packet) == type && xe_spv_packet_result(&packet) == result);
   CHECK(xe_spv_packet_prefix(&packet) == prefix && packet.capacity == 7 - prefix);
   for (i = 0; i < packet.capacity; ++i)
      xe_spv_packet_append_reserved(&packet, (uint32_t)(i * 17), i & 1);
   CHECK(allocations == before && !packet.heap);
   view = xe_spv_packet_view(&packet);
   CHECK(view.count == 7 && view.words[0] == ((7u << 16) | 128));
   stable = view.words;
   fail_next = 1;
   CHECK(!xe_spv_packet_reserve_with(&packet, 64, NULL, NULL));
   CHECK(xe_spv_packet_view(&packet).words == stable && packet.count == 7 - prefix);
   CHECK(xe_spv_packet_reserve_with(&packet, 64, NULL, NULL));
   CHECK(packet.heap && allocations == before + 2 && live == 1);
   while (packet.count < 64) {
      i = packet.count;
      xe_spv_packet_append_reserved(&packet, (uint32_t)(i * 17), i & 1);
   }
   CHECK(xe_spv_packet_reserve_with(&packet, 1024, NULL, NULL));
   CHECK(xe_spv_packet_opcode(&packet) == 128 && xe_spv_packet_type(&packet) == type &&
         xe_spv_packet_result(&packet) == result);
   for (i = 0; i < 64; ++i) {
      CHECK(xe_spv_packet_const_operands(&packet)[i] == i * 17);
      CHECK(xe_spv_packet_is_id(&packet, (uint32_t)i) == (int)(i & 1));
   }
   xe_spv_packet_operands(&packet)[33] = UINT32_MAX;
   CHECK(xe_spv_packet_view(&packet).words[prefix + 33] == UINT32_MAX);
   stable = xe_spv_packet_const_words(&packet);
   before = allocations;
   CHECK(!xe_spv_packet_reserve_with(&packet, SIZE_MAX, NULL, NULL));
   CHECK(!xe_spv_packet_string_capacity(&packet, SIZE_MAX));
   CHECK(xe_spv_packet_const_words(&packet) == stable && allocations == before);
   xe_spv_packet_free(&packet);
   CHECK(!live && packet.arena_offset == 128 && !packet.heap);
   xe_spv_packet_free(&packet);
   CHECK(!live && packet.arena_offset == 128);
   return 0;
}
static int strings(void) {
   xe_spv_packet packet;
   unsigned prefix, bytes, i, j;
   char text[1057];
   for (i = 0; i < sizeof(text); ++i) text[i] = (char)(33 + i % 90);
   for (prefix = 0; prefix < 11; ++prefix) {
      for (bytes = 0; bytes < sizeof(text); ++bytes) {
         size_t added = bytes / 4 + 1;
         xe_spv_packet_init(&packet, 5, 0, 0);
         CHECK(xe_spv_packet_reserve_with(&packet, prefix + added, NULL, NULL));
         for (i = 0; i < prefix; ++i) xe_spv_packet_append_reserved(&packet, i, 1);
         CHECK(xe_spv_packet_string_reserved(&packet, text, bytes));
         CHECK(!memcmp(xe_spv_packet_const_operands(&packet) + prefix, text, bytes));
         for (j = bytes; j < added * 4; ++j)
            CHECK(((const unsigned char *)(xe_spv_packet_const_operands(&packet) + prefix))[j] == 0);
         for (j = 0; j < prefix + added; ++j)
            CHECK(xe_spv_packet_is_id(&packet, j) == (int)(j < prefix));
         CHECK(xe_spv_packet_view(&packet).words[0] == ((1u + prefix + (uint32_t)added) << 16 | 5));
         xe_spv_packet_free(&packet);
      }
   }
   CHECK(!live);
   return 0;
}
static int constant_keys(void) {
   xe_spv_packet packet;
   uint32_t values[64];
   unsigned count, i;
   for (i = 0; i < 64; ++i) values[i] = UINT32_C(0x80000000) ^ (i * UINT32_C(0x7fc00001));
   for (count = 0; count <= 64; ++count) {
      xe_spv_packet_init(&packet, 44, 17, 31);
      CHECK(xe_spv_packet_reserve_with(&packet, count, NULL, NULL));
      for (i = 0; i < count; ++i) xe_spv_packet_append_reserved(&packet, values[i], 0);
      CHECK(xe_spv_packet_constant_equal(&packet, 44, 17, values, count));
      CHECK(xe_spv_packet_typed_result(&packet) == 31);
      CHECK(!xe_spv_packet_constant_equal(&packet, 45, 17, values, count));
      CHECK(!xe_spv_packet_constant_equal(&packet, 44, 18, values, count));
      CHECK(!xe_spv_packet_constant_equal(&packet, 44, 17, values, SIZE_MAX));
      for (i = 0; i < count; ++i) {
         values[i] ^= 1;
         CHECK(!xe_spv_packet_constant_equal(&packet, 44, 17, values, count));
         values[i] ^= 1;
      }
      xe_spv_packet_free(&packet);
   }
   xe_spv_packet_init(&packet, 44, 17, 0);
   CHECK(!xe_spv_packet_constant_equal(&packet, 44, 17, NULL, 0));
   xe_spv_packet_init(&packet, 44, 0, 31);
   CHECK(!xe_spv_packet_constant_equal(&packet, 44, 0, NULL, 0));
   xe_spv_packet_init(&packet, 65536u + 44u, 17, 31);
   CHECK(!xe_spv_packet_constant_equal(&packet, 44, 17, NULL, 0));
   CHECK(!live);
   return 0;
}
int main(void) {
   xe_spv_packet packet;
   unsigned i;
   CHECK(!layout(0,0) && !layout(7,0) && !layout(0,9) && !layout(7,9));
   CHECK(!strings());
   CHECK(!constant_keys());
   xe_spv_packet_init(&packet, 65536, 7, 9);
   CHECK(xe_spv_packet_opcode(&packet) == 65536 && !xe_spv_packet_valid(&packet));
   xe_spv_packet_init(&packet, 128, 7, 9);
   CHECK(xe_spv_packet_reserve_with(&packet, 65533, NULL, NULL));
   for (i = 0; i < 65532; ++i) xe_spv_packet_append_reserved(&packet, i, i & 1);
   CHECK(xe_spv_packet_valid(&packet) && xe_spv_packet_view(&packet).count == 65535);
   xe_spv_packet_append_reserved(&packet, 1, 0);
   CHECK(!xe_spv_packet_valid(&packet));
   xe_spv_packet_free(&packet);
   CHECK(!live);
   puts("PASS: encoded prefixes, inline/growing words and ID flags, patches, faults, strings, wire limits and preserved arena tag");
   return 0;
}
