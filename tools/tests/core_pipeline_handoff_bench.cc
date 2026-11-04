/* Typed interop fixture; the production C89 pools perform all scheduling. */
#include "xenia/gpu/pipeline_creation_queue.h"
#include "core_pipeline_bench_profile.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
struct LargeRequest { uint64_t words[10]; };
static uint64_t id(const uint64_t& request) { return request; }
static uint64_t id(const LargeRequest& request) { return request.words[0]; }
static void fill(uint64_t& request, uint64_t value) { request = value; }
static void fill(LargeRequest& request, uint64_t value) {
  request.words[0] = value;
  for (unsigned i = 1; i < 10; ++i) request.words[i] = value * 13 + i;
}
static uint64_t create(const uint64_t& request) { return request + 1; }
static uint64_t create(const LargeRequest& request) {
  uint64_t sum = request.words[0] + 1;
  for (unsigned i = 1; i < 10; ++i) sum ^= request.words[i];
  return sum;
}
template <typename T> static void measure(const char* label, unsigned workers) {
  uint64_t expected = 0, next = 0, samples[5];
  size_t active;
  {
    xe::gpu::PipelineCreationQueue<T, uint64_t, unsigned> queue;
    queue.Initialize("Handoff bench", [](const T& request, unsigned*) {
      return create(request);
    }, [&](const T& request, uint64_t handle) {
      if (id(request) != expected++ || handle != create(request)) std::abort();
    }, {});
    queue.SetThreadCount(workers);
    active = xe_bench_live();
    for (unsigned round = 0; round < 6; ++round) {
      size_t count = round ? 100000 : 20000;
      size_t before = xe_bench_allocations();
      uint64_t start = xe_bench_time();
      for (size_t i = 0; i < count; ++i) {
        T request;
        fill(request, next++);
        queue.Push(request);
      }
      queue.AwaitCompletion();
      uint64_t elapsed = xe_bench_time() - start;
      if (round) samples[round - 1] = elapsed;
      if (xe_bench_allocations() != before || expected != next) std::abort();
    }
    queue.Shutdown([](const T&, uint64_t) {});
  }
  if (xe_bench_live()) std::abort();
  xe_bench_report(label, sizeof(T), workers, active, samples, 5, 100000);
}
int main(int argc, char** argv) {
  const char* label = argc > 1 ? argv[1] : "current";
  measure<uint64_t>(label, 1);
  measure<LargeRequest>(label, 1);
  measure<uint64_t>(label, 6);
  measure<LargeRequest>(label, 6);
}
