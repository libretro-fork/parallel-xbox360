#include "libretro/core_state_io.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %u\n", (unsigned)__LINE__); exit(1); } } while (0)
int main(void)
{
   unsigned char bytes[18], output[16];
   xe_state_cursor cursor;
   size_t i;
   const void *view;
   xe_state_text text;
   memset(bytes, 0xA5, sizeof(bytes));
   memset(output, 0x3C, sizeof(output));
   cursor.data = bytes + 1; cursor.size = 16; cursor.offset = 0; cursor.measuring = 0;
   CHECK(xe_state_write(&cursor, output, 16));
   CHECK(bytes[0] == 0xA5 && bytes[17] == 0xA5);
   CHECK(!xe_state_write(&cursor, output, 1));
   CHECK(!xe_state_advance(&cursor, (size_t)-1));
   CHECK(cursor.offset == 16);
   CHECK(!xe_state_seek(&cursor, 17));
   CHECK(xe_state_seek(&cursor, 0));
   CHECK(!xe_state_read(&cursor, output, 17));
   for (i = 0; i < sizeof(output); ++i) CHECK(output[i] == 0x3C);
   CHECK(cursor.offset == 0);
   CHECK(!xe_state_patch(&cursor, 0, output, 1));
   CHECK(xe_state_read(&cursor, output, 16));
   CHECK(xe_state_seek(&cursor, 0));
   CHECK(xe_state_view(&cursor, &view, 16));
   CHECK(view == bytes + 1);
   CHECK(!xe_state_view(&cursor, &view, 1));
   CHECK(xe_state_patch(&cursor, 15, "Z", 1));
   CHECK(bytes[16] == 'Z');
   CHECK(!xe_state_patch(&cursor, 15, "XY", 2));
   CHECK(xe_state_read(&cursor, NULL, 0));
   CHECK(xe_state_write(&cursor, NULL, 0));
   CHECK(!xe_state_read(&cursor, NULL, 1));
   CHECK(xe_state_seek(&cursor, 0));
   CHECK(xe_state_write_text(&cursor, "abc", 3));
   CHECK(xe_state_seek(&cursor, 0));
   CHECK(xe_state_read_text(&cursor, &text));
   CHECK(text.size == 3 && text.data == (const char *)bytes + 5);
   CHECK(!memcmp(text.data, "abc", 3));
   CHECK(xe_state_seek(&cursor, 0));
   cursor.size = 6;
   CHECK(!xe_state_read_text(&cursor, &text));
   CHECK(cursor.offset == 0);
   cursor.data = NULL; cursor.size = (size_t)-1; cursor.offset = 0; cursor.measuring = 1;
   CHECK(xe_state_write(&cursor, NULL, (size_t)-2));
   CHECK(!xe_state_write(&cursor, NULL, 2));
   CHECK(cursor.offset == (size_t)-2);
   CHECK(xe_state_write(&cursor, NULL, 1));
   CHECK(!xe_state_write(&cursor, NULL, 1));
   CHECK(xe_state_patch(&cursor, 0, NULL, sizeof(size_t)));
   CHECK(!xe_state_read(&cursor, output, 0));
   puts("PASS: exact bounds, overflow, counting and backpatching without touching guard bytes");
   return 0;
}
