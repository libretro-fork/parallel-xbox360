/* C++ lifetime boundary only. Packet algorithms are tested in the C89 leaf. */
#include <cstdio>
#include <cstdlib>
#include <new>
static unsigned live;
static void* owner_alloc(size_t bytes) { void* p = std::malloc(bytes); if (p) ++live; return p; }
static void owner_free(void* p) { if (p) --live; std::free(p); }
#define XE_COMMAND_STREAM_ALLOC owner_alloc
#define XE_COMMAND_STREAM_FREE owner_free
#include "core_command_stream.h"
struct Context {
  xe_command_stream_owner stream;
  Context(bool fail) {
    if (!xe_command_stream_init(&stream, 64) ||
        !xe_command_stream_append(&stream, 64) ||
        !xe_command_stream_append(&stream, 128)) std::abort();
    if (fail) throw std::bad_alloc();
  }
};
int main() {
  { Context context(false); if (live != 2) return 1; }
  if (live) return 2;
  try { Context context(true); return 3; } catch (const std::bad_alloc&) {}
  if (live) return 4;
  std::puts("Command stream C++ owner: normal and throwing-constructor page teardown pass");
}
