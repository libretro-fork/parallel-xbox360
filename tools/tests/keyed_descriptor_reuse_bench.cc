/* CPU metadata benchmark. C++ containers implement the legacy reference only. */
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <memory>
#include <unordered_map>
#include <vector>
static size_t old_allocs, old_live, new_allocs, new_live;
struct Allocation { void* p; size_t bytes; };
static Allocation pages[4096];
static void* page_alloc(size_t bytes) {
  void* p = std::malloc(bytes); if (!p) std::abort();
  for (auto& a : pages) if (!a.p) { a = {p, bytes}; ++new_allocs; new_live += bytes; return p; }
  std::abort();
}
static void page_free(void* p) {
  if (!p) return;
  for (auto& a : pages) if (a.p == p) { new_live -= a.bytes; a.p = nullptr; std::free(p); return; }
  std::abort();
}
#define XE_KEYED_REUSE_ALLOC page_alloc
#define XE_KEYED_REUSE_FREE page_free
#include "core_keyed_descriptor_reuse.h"
template<class T> struct Allocator {
  using value_type = T;
  Allocator() = default;
  template<class U> Allocator(const Allocator<U>&) {}
  T* allocate(size_t n) { T* p = std::allocator<T>{}.allocate(n); ++old_allocs; old_live += n * sizeof(T); return p; }
  void deallocate(T* p, size_t n) { old_live -= n * sizeof(T); std::allocator<T>{}.deallocate(p, n); }
  template<class U> bool operator==(const Allocator<U>&) const { return true; }
  template<class U> bool operator!=(const Allocator<U>&) const { return false; }
};
struct Used { uint64_t frame; uint32_t key; uint64_t handle; };
struct Result { double ms; size_t allocations, bytes; uint64_t sum; };
static uint32_t key_for(unsigned mode, uint64_t frame, unsigned draw) {
  uint32_t kind = mode ? (uint32_t(frame) * 73u + draw * 37u) & 255u : draw & 1u;
  return kind * 65537u ^ ((kind & 1) << 31);
}
static Result legacy(unsigned mode) {
  std::deque<Used, Allocator<Used>> used;
  using Free = std::vector<uint64_t, Allocator<uint64_t>>;
  using Pair = std::pair<const uint32_t, Free>;
  std::unordered_map<uint32_t, Free, std::hash<uint32_t>, std::equal_to<uint32_t>, Allocator<Pair>> free;
  uint64_t id = 0, sum = 0;
  auto step = [&](uint64_t frame) {
    uint64_t completed = frame > 2 ? frame - 2 : 0;
    while (!used.empty() && used.front().frame <= completed) {
      free[used.front().key].push_back(used.front().handle); used.pop_front();
    }
    for (unsigned draw = 0; draw < 64; ++draw) {
      uint32_t key = key_for(mode, frame, draw);
      uint64_t handle;
      auto it = free.find(key);
      if (it != free.end() && !it->second.empty()) { handle = it->second.back(); it->second.pop_back(); }
      else handle = ++id;
      used.push_back({frame, key, handle}); sum ^= handle + frame * 131 + key;
    }
  };
  for (uint64_t f = 1; f <= 1000; ++f) step(f);
  size_t before = old_allocs, bytes = old_live + sizeof(used) + sizeof(free);
  std::clock_t start = std::clock();
  for (uint64_t f = 1001; f <= 501000; ++f) step(f);
  return {1000.0 * (std::clock() - start) / CLOCKS_PER_SEC, old_allocs - before, bytes, sum};
}
static Result stable(unsigned mode) {
  xe_keyed_descriptor_reuse pool{};
  uint64_t id = 0, sum = 0;
  if (!xe_keyed_reuse_warm(&pool)) std::abort();
  auto step = [&](uint64_t frame) {
    xe_keyed_reuse_collect(&pool, frame > 2 ? frame - 2 : 0);
    for (unsigned draw = 0; draw < 64; ++draw) {
      uint32_t key = key_for(mode, frame, draw);
      auto* node = xe_keyed_reuse_acquire(&pool, key, frame); if (!node) std::abort();
      if (!node->handle) node->handle = ++id;
      sum ^= node->handle + frame * 131 + key;
      if (!xe_keyed_reuse_commit(&pool)) std::abort();
    }
  };
  for (uint64_t f = 1; f <= 1000; ++f) step(f);
  size_t before = new_allocs, bytes = new_live + sizeof(pool);
  std::clock_t start = std::clock();
  for (uint64_t f = 1001; f <= 501000; ++f) step(f);
  Result result{1000.0 * (std::clock() - start) / CLOCKS_PER_SEC, new_allocs - before, bytes, sum};
  if (!xe_keyed_reuse_destroy(&pool)) std::abort();
  return result;
}
int main() {
  for (unsigned mode = 0; mode < 2; ++mode) {
    double old_times[3], new_times[3];
    Result a{}, b{};
    for (unsigned i = 0; i < 3; ++i) {
      a = legacy(mode); b = stable(mode);
      if (a.sum != b.sum || b.allocations || old_live || new_live) return 1;
      old_times[i] = a.ms; new_times[i] = b.ms;
    }
    std::sort(old_times, old_times + 3); std::sort(new_times, new_times + 3);
    std::printf("%u keys, 32M leases, median 3 CPU runs: legacy %.3f ms / %zu allocations / %zu warm bytes; C89 %.3f ms / %zu allocations / %zu warm bytes\n",
        mode ? 256 : 2, old_times[1], a.allocations, a.bytes, new_times[1], b.allocations, b.bytes);
  }
  return 0;
}
