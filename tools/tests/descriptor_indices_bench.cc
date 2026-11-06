/* Driverless metadata benchmark; C++ containers model only the legacy path. */
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <memory>
#include <vector>
static size_t old_allocs, old_live, new_allocs, new_live;
static void* index_alloc(size_t bytes) {
  void* p = std::malloc(bytes); if (!p) std::abort();
  ++new_allocs; new_live += bytes; return p;
}
static void index_free(void* p) {
  if (p) { new_live = 0; std::free(p); }
}
#define XE_DESCRIPTOR_INDEX_ALLOC index_alloc
#define XE_DESCRIPTOR_INDEX_FREE index_free
#include "core_descriptor_indices.h"
template<class T> struct Allocator {
  using value_type = T;
  Allocator() = default;
  template<class U> Allocator(const Allocator<U>&) {}
  T* allocate(size_t n) {
    T* p = std::allocator<T>{}.allocate(n); ++old_allocs; old_live += n * sizeof(T); return p;
  }
  void deallocate(T* p, size_t n) { old_live -= n * sizeof(T); std::allocator<T>{}.deallocate(p, n); }
  template<class U> bool operator==(const Allocator<U>&) const { return true; }
  template<class U> bool operator!=(const Allocator<U>&) const { return false; }
};
struct Used { uint32_t index; uint64_t submission; };
struct Result { double ms; size_t allocations, bytes; uint64_t sum; };
static const uint32_t capacity = 262144, reserved = 8;
static Result legacy(unsigned mode) {
  std::vector<uint32_t, Allocator<uint32_t>> free;
  std::deque<Used, Allocator<Used>> used;
  uint32_t allocated = reserved;
  uint64_t sum = 0;
  auto take = [&]() {
    if (free.empty()) { if (allocated == capacity) std::abort(); return allocated++; }
    uint32_t id = free.back(); free.pop_back(); return id;
  };
  auto step = [&](uint64_t submission) {
    while (!used.empty() && used.front().submission <= submission - 2) {
      free.push_back(used.front().index); used.pop_front();
    }
    uint32_t persistent[64];
    if (mode) {
      for (unsigned i = 0; i < 64; ++i) { persistent[i] = take(); sum += persistent[i]; }
      for (unsigned i = 0; i < 64; ++i) free.push_back(persistent[i]);
    }
    if (mode != 2) for (unsigned i = 0; i < 64; ++i) {
      uint32_t id = take(); sum += id; used.push_back({id, submission});
    }
  };
  for (uint64_t s = 3; s < 1003; ++s) step(s);
  size_t before = old_allocs, bytes = old_live + sizeof(free) + sizeof(used) + sizeof(allocated);
  std::clock_t start = std::clock();
  for (uint64_t s = 1003; s < 501003; ++s) step(s);
  return {1000.0 * (std::clock() - start) / CLOCKS_PER_SEC, old_allocs - before, bytes, sum};
}
static Result stable(unsigned mode) {
  xe_descriptor_indices_owner pool;
  xe_descriptor_index_batch current, owners[4];
  uint64_t sum = 0;
  if (!xe_descriptor_indices_init(&pool, capacity, reserved)) std::abort();
  xe_descriptor_index_batch_clear(&current);
  for (auto& owner : owners) xe_descriptor_index_batch_clear(&owner);
  auto step = [&](uint64_t submission) {
    if (!xe_descriptor_index_batch_recycle(&pool, &owners[(submission - 2) % 4])) std::abort();
    uint32_t persistent[64];
    if (mode) {
      for (unsigned i = 0; i < 64; ++i) { persistent[i] = xe_descriptor_index_take(&pool); sum += persistent[i]; }
      for (unsigned i = 0; i < 64; ++i) if (!xe_descriptor_index_release(&pool, persistent[i])) std::abort();
    }
    if (mode != 2) for (unsigned i = 0; i < 64; ++i) {
      uint32_t id = xe_descriptor_index_take_one_use(&pool, &current);
      if (id == UINT32_MAX) std::abort();
      sum += id;
    }
    if (!xe_descriptor_index_batch_move(&owners[submission % 4], &current)) std::abort();
  };
  for (uint64_t s = 3; s < 1003; ++s) step(s);
  size_t before = new_allocs, bytes = new_live + sizeof(pool) + sizeof(current) + sizeof(owners);
  std::clock_t start = std::clock();
  for (uint64_t s = 1003; s < 501003; ++s) step(s);
  return {1000.0 * (std::clock() - start) / CLOCKS_PER_SEC, new_allocs - before, bytes, sum};
}
int main() {
  const char* names[] = {"transient", "mixed", "persistent"};
  for (unsigned mode = 0; mode < 3; ++mode) {
    double old_times[3], new_times[3]; Result a{}, b{};
    for (unsigned run = 0; run < 3; ++run) {
      a = legacy(mode); b = stable(mode);
      if (a.sum != b.sum || b.allocations || old_live || new_live) return 1;
      old_times[run] = a.ms; new_times[run] = b.ms;
    }
    std::sort(old_times, old_times + 3); std::sort(new_times, new_times + 3);
    std::printf("%s, 500k submissions, median 3 CPU runs: legacy %.3f ms / %zu timed allocations / %zu warm requested bytes; C89 %.3f ms / %zu timed allocations / %zu warm requested bytes\n",
      names[mode], old_times[1], a.allocations, a.bytes, new_times[1], b.allocations, b.bytes);
  }
}
