/* Legacy C++ vector reference and CPU metadata benchmark; no GPU calls. */
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <memory>
#include <utility>
#include <vector>
#include "core_upload_ranges.h"
static size_t allocations, live_bytes;
template<class T> struct Allocator {
  using value_type = T;
  Allocator() = default;
  template<class U> Allocator(const Allocator<U>&) {}
  T* allocate(size_t n) { T* p = std::allocator<T>{}.allocate(n); ++allocations; live_bytes += n * sizeof(T); return p; }
  void deallocate(T* p, size_t n) { live_bytes -= n * sizeof(T); std::allocator<T>{}.deallocate(p, n); }
  template<class U> bool operator==(const Allocator<U>&) const { return true; }
  template<class U> bool operator!=(const Allocator<U>&) const { return false; }
};
#ifdef _MSC_VER
#define NOINLINE __declspec(noinline)
#else
#define NOINLINE __attribute__((noinline))
#endif
template<class T> static NOINLINE uint64_t consume(const T* data, uint32_t count) {
  uint64_t sum = 0;
  for (uint32_t i = 0; i < count; ++i) sum += uint64_t(data[i].first) * 131 + data[i].second;
  return sum;
}
static bool keep(unsigned mode, uint32_t index) { return mode == 0 || (mode == 1 && (index & 1)); }
struct Result { double ms; size_t cold, timed, bytes, copies; uint64_t sum; };
static Result legacy(unsigned mode, uint32_t count) {
  using Pair = std::pair<uint32_t, uint32_t>;
  Pair input[4096];
  std::vector<Pair, Allocator<Pair>> output;
  uint64_t sum = 0;
  size_t copies = 0, cold_start = allocations;
  auto step = [&](uint32_t batch, bool timed) {
    for (uint32_t i = 0; i < count; ++i) { input[i].first = i * 4; input[i].second = ((batch + i) & 3) + 1; }
    output.clear();
    for (uint32_t i = 0; i < count; ++i) if (keep(mode, input[i].first / 4)) {
      output.push_back(input[i]); if (timed) ++copies;
    }
    sum += consume(output.data(), uint32_t(output.size()));
  };
  for (uint32_t b = 0; b < 1000; ++b) step(b, false);
  size_t before = allocations, bytes = live_bytes;
  std::clock_t start = std::clock();
  for (uint32_t b = 1000; b < 1000 + 33554432u / count; ++b) step(b, true);
  return {1000.0 * (std::clock() - start) / CLOCKS_PER_SEC, before - cold_start, allocations - before, bytes, copies, sum};
}
static Result borrowed(unsigned mode, uint32_t count) {
  xe_upload_range input[4096];
  uint64_t sum = 0;
  size_t copies = 0;
  auto step = [&](uint32_t batch, bool timed) {
    for (uint32_t i = 0; i < count; ++i) { input[i].first = i * 4; input[i].second = ((batch + i) & 3) + 1; }
    uint32_t written = 0;
    for (uint32_t i = 0; i < count; ++i) if (keep(mode, input[i].first / 4)) {
      int moved = xe_upload_range_keep(input, i, &written); if (timed) copies += unsigned(moved);
    }
    sum += consume(input, written);
  };
  for (uint32_t b = 0; b < 1000; ++b) step(b, false);
  std::clock_t start = std::clock();
  for (uint32_t b = 1000; b < 1000 + 33554432u / count; ++b) step(b, true);
  return {1000.0 * (std::clock() - start) / CLOCKS_PER_SEC, 0, 0, 0, copies, sum};
}
int main() {
  const char* names[] = {"all CPU", "half GPU", "all GPU"};
  for (uint32_t count : {64u, 4096u}) for (unsigned mode = 0; mode < 3; ++mode) {
    double a_times[3], b_times[3]; Result a{}, b{};
    for (unsigned run = 0; run < 3; ++run) {
      a = legacy(mode, count); b = borrowed(mode, count);
      if (a.sum != b.sum || live_bytes || a.timed || b.timed) return 1;
      a_times[run] = a.ms; b_times[run] = b.ms;
    }
    std::sort(a_times, a_times + 3); std::sort(b_times, b_times + 3);
    std::printf("%u ranges, %s, 32Mi visits median3: vector %.3f ms / %zu cold allocations / %zu retained bytes / %zu copied records; C89 %.3f ms / 0 cold allocations / 0 extra bytes / %zu moved records\n",
        count, names[mode], a_times[1], a.cold, a.bytes, a.copies, b_times[1], b.copies);
  }
}
