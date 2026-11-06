/* CPU-only paired benchmark: reuse the real-allocator fake-dispatch fixture.
 * Both prior/current builds include identical fixture and timing code. */
#define main xe_pool_fixture_main
#include "vk_pool_owner_bridge.cc"
#undef main
#include <ctime>
int main() {
  double ms[3], swap;
  uint64_t expected;
  native_errors = 0;
  for (unsigned mode = 0; mode < 2; ++mode) {
    hot_workload = (int)mode;
    for (unsigned i = 0; i < 5; ++i) if (run(0, 0, nullptr, nullptr)) return 1;
    expected = trace;
    for (unsigned trial = 0; trial < 3; ++trial) {
      std::clock_t begin = std::clock();
      for (unsigned i = 0; i < 10000; ++i) if (run(0, 0, nullptr, nullptr)) return 1;
      ms[trial] = 1000.0 * double(std::clock() - begin) / CLOCKS_PER_SEC;
    }
    if (ms[0] > ms[1]) { swap = ms[0]; ms[0] = ms[1]; ms[1] = swap; }
    if (ms[1] > ms[2]) { swap = ms[1]; ms[1] = ms[2]; ms[2] = swap; }
    if (ms[0] > ms[1]) { swap = ms[0]; ms[0] = ms[1]; ms[1] = swap; }
    std::printf("%s: 15M requests, median 3 CPU runs %.3f ms; per 1500 requests C++ allocs=%zu C89 allocs=%zu; trace=%016llx\n",
        mode ? "hot latest" : "mixed search", ms[1], new_calls, metadata_calls, (unsigned long long)trace);
    if (trace != expected) return 1;
  }
  return 0;
}
