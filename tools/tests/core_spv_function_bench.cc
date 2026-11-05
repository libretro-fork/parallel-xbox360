/* Typed factory/ownership interop over the production builder. */
#include "core_pipeline_bench_profile.h"
#include "SpvBuilder.h"
#include "InstructionArena.h"
#include <new>
#include <cstdio>
#include <cstdlib>
void* operator new(size_t bytes) {
  if (void* value = xe_bench_malloc(bytes ? bytes : 1)) return value;
  throw std::bad_alloc();
}
void* operator new[](size_t bytes) { return ::operator new(bytes); }
void operator delete(void* value) noexcept { xe_bench_free(value); }
void operator delete[](void* value) noexcept { xe_bench_free(value); }
void operator delete(void* value, size_t) noexcept { xe_bench_free(value); }
void operator delete[](void* value, size_t) noexcept { xe_bench_free(value); }
static void build_functions(xe_spv_arena* arena, unsigned count, unsigned parameterCount, xe_spv_binary* output) {
  spv::Builder builder(spv::Spv_1_3, 0, nullptr, arena);
  builder.addCapability(spv::Capability::Shader);
  builder.setMemoryModel(spv::AddressingModel::Logical, spv::MemoryModel::GLSL450);
  auto* main = builder.makeEntryPoint("main");
  builder.addEntryPoint(spv::ExecutionModel::GLCompute, main, "main");
  builder.addExecutionMode(main, spv::ExecutionMode::LocalSize, 1, 1, 1);
  builder.makeReturn(true);
  builder.leaveFunction();
  std::vector<spv::Id> parameterTypes;
  for (unsigned p = 0; p < parameterCount; ++p)
    parameterTypes.push_back((p & 1) ? builder.makeUintType(32) : builder.makeFloatType(32));
  for (unsigned i = 1; i < count; ++i) {
    spv::Block* entry;
#ifdef XE_SPV_LONG_NAME_BENCH
    const char* name = "worker_generated_function_without_linkage_which_needs_no_temporary_string";
#else
    const char* name = "f";
#endif
    builder.makeFunctionEntry(spv::NoPrecision, builder.makeVoidType(), name,
        spv::LinkageType::Max, parameterTypes, {}, &entry);
    builder.makeReturn(true);
    builder.leaveFunction();
  }
  if (output && !builder.dumpSized(*output)) throw std::bad_alloc();
}
int main(int argc, char** argv) {
  if (argc != 3) return 1;
  unsigned variant = unsigned(std::strtoul(argv[2], nullptr, 10));
  unsigned count = variant == 0 ? 1 : variant == 4 ? 71 : variant == 5 ? 257 : 0;
  if (!count) return 2;
#ifdef XE_SPV_PARAMETER_BENCH
  const unsigned parameterCount = variant == 4 ? 9 : variant == 5 ? 70 : 0;
#else
  const unsigned parameterCount = 0;
#endif
  const size_t initial = xe_bench_live();
  xe_spv_binary output = {};
  {
    spv::InstructionArena workspace(count * 4 + 64);
    build_functions(workspace.get(), count, parameterCount, &output);
    const size_t warmed = xe_bench_live();
    const size_t pages = workspace.get()->reserved_records;
    size_t calls = xe_bench_allocations(), requested = xe_bench_requested();
    uint64_t start = xe_bench_time();
    for (unsigned repeat = 0; repeat < 1000; ++repeat)
      build_functions(workspace.get(), count, parameterCount, nullptr);
    uint64_t elapsed = xe_bench_time() - start;
    if (xe_bench_live() != warmed || workspace.get()->reserved_records != pages) return 3;
#ifdef XE_SPV_BLOCK_CHAIN_BENCH
    std::printf("block_bytes=%zu function_bytes=%zu builder_bytes=%zu peak_tracked=%zu ",
        sizeof(spv::Block), sizeof(spv::Function), sizeof(spv::Builder), xe_bench_peak() - initial);
#endif
#ifdef XE_SPV_PARAMETER_BENCH
    std::printf("parameters=%u function_bytes=%zu peak_tracked=%zu ",
        parameterCount, sizeof(spv::Function), xe_bench_peak() - initial);
#endif
    std::printf("functions=%u modules=1000 module_bytes=%zu calls=%zu requested=%zu retained=%zu ms=%.3f\n",
        count, sizeof(spv::Module), xe_bench_allocations() - calls,
        xe_bench_requested() - requested, warmed - initial, double(elapsed) / 1000.0);
  }
  FILE* file = std::fopen(argv[1], "wb");
  if (!file) return 4;
  bool ok = std::fwrite(output.words, sizeof(uint32_t), output.count, file) == output.count;
  if (std::fclose(file)) ok = false;
  xe_spv_binary_close(&output);
  if (xe_bench_live() != initial) return 5;
  return ok ? 0 : 6;
}
