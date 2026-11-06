/* C++ lifetime boundary only; the scan/storage algorithms are C89. */
#include <cstdio>
#include <cstdlib>
#include <new>
static unsigned live;
static void* owner_alloc(size_t n) { void* p = std::malloc(n); if (p) ++live; return p; }
static void owner_free(void* p) { if (p) --live; std::free(p); }
#define XE_UPLOAD_SCAN_ALLOC owner_alloc
#define XE_UPLOAD_SCAN_FREE owner_free
#include "core_upload_scan.h"
struct Context {
  xe_upload_scan_owner ranges;
  Context(bool fail) {
    if (!xe_upload_scan_init(&ranges, 131072)) std::abort();
    if (fail) throw std::bad_alloc();
  }
};
int main() {
  { Context context(false); if (live != 1) return 1; }
  if (live) return 2;
  try { Context context(true); return 3; } catch (const std::bad_alloc&) {}
  if (live) return 4;
  std::puts("Upload scan C++ owner: normal and throwing-constructor teardown pass");
}
