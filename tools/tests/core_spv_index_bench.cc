/* Production typed factory/lookup/destruction interop. Counters and timing are
 * benchmark-only; the production allocator has no profiling counters. */
#include "core_pipeline_bench_profile.h"
#include "spvIR.h"
#include "InstructionArena.h"
#include <new>
#include <cstdio>
void* operator new(size_t bytes) {
  if (void* value = xe_bench_malloc(bytes ? bytes : 1)) return value;
  throw std::bad_alloc();
}
void* operator new[](size_t bytes) { return ::operator new(bytes); }
void operator delete(void* value) noexcept { xe_bench_free(value); }
void operator delete[](void* value) noexcept { xe_bench_free(value); }
void operator delete(void* value, size_t) noexcept { xe_bench_free(value); }
void operator delete[](void* value, size_t) noexcept { xe_bench_free(value); }
static uint64_t compile_ids(xe_spv_arena* arena, unsigned count) {
  spv::Module module(arena);
  uint64_t sum = 0;
  for (unsigned i = 0; i < count; ++i) {
    unsigned id = i * 3 + 1;
    auto* instruction = module.createInstruction(id, spv::NoType, spv::Op::OpNop);
    module.mapInstruction(instruction);
    sum += module.getInstruction(id)->getResultId();
    if (module.getInstruction(id - 1)) throw std::bad_alloc();
  }
  return sum;
}
int main() {
  std::printf("module_bytes=%zu workspace_bytes=%zu\n", sizeof(spv::Module), sizeof(xe_spv_arena));
  const unsigned sizes[] = {128, 2048, 16384};
  for (unsigned count : sizes) {
    const size_t initial = xe_bench_live();
    {
      spv::InstructionArena workspace(count);
      const uint64_t expected = compile_ids(workspace.get(), count);
      const size_t warmed = xe_bench_live();
      for (unsigned sample = 0; sample < 5; ++sample) {
        size_t calls = xe_bench_allocations(), requested = xe_bench_requested();
        uint64_t start = xe_bench_time(), sum = 0;
        for (unsigned repeat = 0; repeat < 1000; ++repeat)
          sum += compile_ids(workspace.get(), count);
        uint64_t elapsed = xe_bench_time() - start;
        if (sum != expected * 1000 || xe_bench_live() != warmed) return 1;
        std::printf("ids=%u modules=1000 calls=%zu requested=%zu retained=%zu ms=%.3f\n",
            count, xe_bench_allocations() - calls, xe_bench_requested() - requested,
            warmed - initial, double(elapsed) / 1000.0);
      }
    }
    if (xe_bench_live() != initial) return 2;
  }
  return 0;
}
