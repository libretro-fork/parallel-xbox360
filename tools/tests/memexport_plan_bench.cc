/* Legacy container/page reference, actual VkBufferCopy fields, CPU only. */
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <utility>
#include <vector>
#include <vulkan/vulkan.h>
#include "core_memexport.h"
#ifdef _MSC_VER
#define NOINLINE __declspec(noinline)
#elif defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 9
#define NOINLINE __attribute__((noinline,noclone,noipa))
#else
#define NOINLINE __attribute__((noinline))
#endif
static size_t allocations, live_bytes;
using BenchClock = std::chrono::steady_clock;
static double elapsed(BenchClock::time_point start) {
  return std::chrono::duration<double, std::milli>(BenchClock::now() - start).count();
}
template<class T> struct Allocator {
  using value_type = T;
  Allocator() = default;
  template<class U> Allocator(const Allocator<U>&) {}
  T* allocate(size_t n) { T* p = std::allocator<T>{}.allocate(n); ++allocations; live_bytes += n * sizeof(T); return p; }
  void deallocate(T* p, size_t n) { live_bytes -= n * sizeof(T); std::allocator<T>{}.deallocate(p, n); }
  template<class U> bool operator==(const Allocator<U>&) const { return true; }
  template<class U> bool operator!=(const Allocator<U>&) const { return false; }
};
struct Legacy {
  std::array<uint64_t, 2048> words{};
  bool any = false;
  uint32_t first = 131072, last = 0;
  std::vector<std::pair<uint32_t,uint32_t>, Allocator<std::pair<uint32_t,uint32_t>>> runs;
  std::vector<VkBufferCopy, Allocator<VkBufferCopy>> regions;
  NOINLINE void mark(uint32_t base, uint32_t size) {
    if (!size || base >= XE_MEMEXPORT_BUFFER_SIZE) return;
    size = std::min(size, XE_MEMEXPORT_BUFFER_SIZE - base);
    uint32_t a = base >> 12, b = (base + size - 1) >> 12;
    for (uint32_t p = a; p <= b; ++p) words[p >> 6] |= uint64_t(1) << (p & 63);
    any = true; first = std::min(first, a); last = std::max(last, b);
  }
  NOINLINE void clear(uint32_t base, uint32_t size) {
    if (!any || !size || base >= XE_MEMEXPORT_BUFFER_SIZE) return;
    size = std::min(size, XE_MEMEXPORT_BUFFER_SIZE - base);
    for (uint32_t p = (base + 4095) >> 12; p < ((base + size) >> 12); ++p) words[p >> 6] &= ~(uint64_t(1) << (p & 63));
  }
  bool has(uint32_t base, uint32_t size) const {
    if (!any || !size || base >= XE_MEMEXPORT_BUFFER_SIZE) return false;
    size = std::min(size, XE_MEMEXPORT_BUFFER_SIZE - base);
    for (uint32_t p = std::max(base >> 12, first); p <= std::min((base + size - 1) >> 12, last); ++p)
      if (words[p >> 6] & (uint64_t(1) << (p & 63))) return true;
    return false;
  }
  bool gather(uint32_t base, uint32_t size, bool whole) {
    runs.clear(); if (!has(base, size)) return false;
    size = std::min(size, XE_MEMEXPORT_BUFFER_SIZE - base);
    if (whole) { runs.emplace_back(base, size); return true; }
    uint32_t end = base + size;
    for (uint32_t p = std::max(base >> 12, first); p <= std::min((end - 1) >> 12, last); ++p) {
      if (!(words[p >> 6] & (uint64_t(1) << (p & 63)))) continue;
      uint32_t a = std::max(base, p << 12), b = std::min(end, (p + 1) << 12);
      if (!runs.empty() && runs.back().first + runs.back().second == a) runs.back().second += b - a;
      else runs.emplace_back(a, b - a);
    }
    return true;
  }
};
static VkBufferCopy final_regions[65536];
static NOINLINE uint64_t consume(uint32_t count, uint32_t base, uint32_t size) {
  uint64_t sum = uint64_t(base) * 17 + size + count;
  for (uint32_t i = 0; i < count; ++i) sum += final_regions[i].srcOffset * 31 + final_regions[i].dstOffset + final_regions[i].size;
  return sum;
}
static NOINLINE uint64_t step_old(Legacy* s, uint32_t base, uint32_t size, bool whole) {
  if (!s->gather(base, size, whole)) return 0;
  s->regions.clear();
  for (auto& run : s->runs) { auto& r = s->regions.emplace_back(); r.srcOffset = r.dstOffset = run.first; r.size = run.second; }
  std::memcpy(final_regions, s->regions.data(), s->regions.size() * sizeof(VkBufferCopy));
  uint32_t first = s->runs.front().first, end = s->runs.back().first + s->runs.back().second;
  return consume(uint32_t(s->regions.size()), first, end - first);
}
static NOINLINE uint64_t step_new(const xe_memexport_map* s, uint32_t base, uint32_t size, bool whole) {
  xe_memexport_plan plan;
  if (!xe_memexport_plan_make(s, base, size, whole, &plan)) return 0;
  xe_memexport_run run{};
  for (uint32_t i = 0; i < plan.count; ++i) {
    /* Same immutable-plan contract as the release emitter, without adding a
     * per-region test-only branch absent from the production command path. */
    (void)xe_memexport_next(&plan.cursor, &run);
    final_regions[i].srcOffset = final_regions[i].dstOffset = run.base; final_regions[i].size = run.size;
  }
  return consume(plan.count, plan.base, plan.size);
}
static NOINLINE void mark_new(xe_memexport_map* s, uint32_t base, uint32_t size) { xe_memexport_mark(s, base, size); }
static NOINLINE void clear_new(xe_memexport_map* s, uint32_t base, uint32_t size) { xe_memexport_clear(s, base, size); }
struct Result { double ms; size_t cold, timed, bytes; uint64_t sum; };
static Result run(unsigned mode, bool stable) {
  Legacy old;
  xe_memexport_map current{};
  old.mark(0, XE_MEMEXPORT_BUFFER_SIZE); xe_memexport_mark(&current, 0, XE_MEMEXPORT_BUFFER_SIZE);
  if (mode == 2) for (unsigned i = 0; i < 2048; ++i) old.words[i] = current.words[i] = UINT64_C(0x5555555555555555);
  if (mode == 3) { old.clear(0, XE_MEMEXPORT_BUFFER_SIZE); xe_memexport_clear(&current, 0, XE_MEMEXPORT_BUFFER_SIZE); }
  uint32_t iterations = mode < 2 ? 500000 : mode == 2 ? 10000 : 50000;
  uint64_t sum = 0;
  size_t initial = allocations, before = initial, bytes = 0;
  BenchClock::time_point start;
  for (uint32_t b = 0; b < iterations + 64; ++b) {
    if (b == 64) { before = allocations; bytes = live_bytes; start = BenchClock::now(); }
    uint32_t base = (b & 63) * 4096 + 17, size = (mode < 2 ? 64u : 4096u) * 4096 - 29;
    sum += stable ? step_new(&current, base, size, mode == 0) : step_old(&old, base, size, mode == 0);
  }
  return {elapsed(start), before - initial, allocations - before, bytes, sum};
}
int main() {
  const char* names[] = {"whole 64 pages", "dense partial 64 pages", "fragmented 4096 pages", "cleared window 4096 pages"};
  for (unsigned mode = 0; mode < 4; ++mode) {
    double a[3], b[3]; Result old{}, current{};
    for (unsigned i = 0; i < 3; ++i) {
      old = run(mode, false); current = run(mode, true);
      if (old.sum != current.sum || old.timed || current.timed || live_bytes) return 1;
      a[i] = old.ms; b[i] = current.ms;
    }
    std::sort(a, a + 3); std::sort(b, b + 3);
    std::printf("%s, median3 CPU: vector/page %.3f ms, %zu cold allocations, %zu extra bytes; C89 direct %.3f ms, 0 cold allocations, 0 extra bytes\n", names[mode], a[1], old.cold, old.bytes, b[1]);
  }
  for (uint32_t pages : {8u, 2048u}) {
    double a[3], b[3];
    uint32_t iterations = pages == 8 ? 1000000 : 65536;
    for (unsigned trial = 0; trial < 3; ++trial) {
      Legacy old; xe_memexport_map current{};
      BenchClock::time_point start = BenchClock::now();
      for (uint32_t i = 0; i < iterations; ++i) { uint32_t base = (i & 63) * 4096; old.mark(base, pages * 4096); old.clear(base, pages * 4096); }
      a[trial] = elapsed(start);
      start = BenchClock::now();
      for (uint32_t i = 0; i < iterations; ++i) { uint32_t base = (i & 63) * 4096; mark_new(&current, base, pages * 4096); clear_new(&current, base, pages * 4096); }
      b[trial] = elapsed(start);
      if (std::memcmp(old.words.data(), current.words, sizeof(current.words))) return 2;
    }
    std::sort(a, a + 3); std::sort(b, b + 3);
    std::printf("%u-page mark+clear pairs: page %.3f ms; C89 word %.3f ms\n", pages, a[1], b[1]);
  }
}
