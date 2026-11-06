// Thin C++ owner teardown only; allocation and snapshot algorithm are MSVC C89.
#include <cstdlib>
#include <cstdio>
#include <stdexcept>
static size_t allocations, frees;
static void* counted_alloc(size_t size) { void* p = std::malloc(size); if (p) ++allocations; return p; }
static void counted_free(void* p) { if (p) ++frees; std::free(p); }
#define XE_RESOLVE_ALLOC counted_alloc
#define XE_RESOLVE_FREE counted_free
#include "core_resolve_readback.h"
struct Failing {
  xe_resolve_readback_owner owner;
  Failing() { if (!xe_resolve_readback_init(&owner, 24)) std::abort(); throw std::runtime_error("fixture"); }
};
int main() {
  { xe_resolve_readback_owner owner; if (!xe_resolve_readback_init(&owner, 0)) return 1; }
  try { Failing value; return 2; } catch (const std::runtime_error&) {}
  if (allocations != 2 || frees != allocations) return 3;
  std::puts("Resolve C++ owner: normal and throwing-constructor CPU block teardown pass");
  return 0;
}
