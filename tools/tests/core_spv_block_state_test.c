#include <stdio.h>
#include <string.h>
#include "../../libretro/core_spv_block_state.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
typedef char debug_state_size_check[sizeof(xe_spv_block_debug) == 16 ? 1 : -1];
int main(void)
{
   xe_spv_block_debug debug;
   uint8_t flags, initial;
   unsigned round;
   int line, column;
   uint32_t file, scope;
   for (round = 0; round < 10000; ++round) {
      memset(&debug, 0xA5, sizeof(debug));
      initial = (uint8_t)((round & 1) ? XE_SPV_BLOCK_UNREACHABLE : 0);
      flags = initial;
      line = (int)(round % 501) - 250;
      column = (int)(round % 39) - 19;
      file = round & 1 ? UINT32_MAX : 0;
      scope = round + 1;
      CHECK(xe_spv_block_source(&debug, &flags, line, column, file));
      CHECK(!xe_spv_block_source(&debug, &flags, line, column, file));
      CHECK(flags == (initial | XE_SPV_BLOCK_LOCATION_VALID));
      CHECK(xe_spv_block_scope(&debug, &flags, scope));
      CHECK(!xe_spv_block_scope(&debug, &flags, scope));
      CHECK(debug.line == line && debug.column == column && debug.file == file);
      CHECK(xe_spv_block_source(&debug, &flags, line + 1, column, file));
      CHECK(xe_spv_block_source(&debug, &flags, line + 1, column + 1, file));
      CHECK(xe_spv_block_source(&debug, &flags, line + 1, column + 1, file ^ 1));
      CHECK(debug.scope == scope && xe_spv_block_scope(&debug, &flags, scope + 1));
      CHECK(flags == (initial | XE_SPV_BLOCK_LOCATION_VALID | XE_SPV_BLOCK_SCOPE_VALID));
   }
   puts("PASS: 10000 C89 block debug state sequences, independent validity, exact change detection and unreachable preservation");
   return 0;
}
