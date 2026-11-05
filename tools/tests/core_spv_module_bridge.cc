/* Full production builder control: deterministic compute modules, no GLSL. */
#if defined(XE_SPV_BINARY_CHECK) && !defined(XE_SPV_BYTE_BENCH)
#include "core_spv_arena_test_alloc.h"
#endif
#include "SpvBuilder.h"
#include "src/xenia/gpu/shader_binary.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef XE_SPV_BYTE_BENCH
#include "core_pipeline_bench_profile.h"
#endif
#ifdef XE_SPV_BINARY_CHECK
#include <type_traits>
#include <new>
static int fail_new_after = -1;
static size_t allocation_calls, allocation_bytes;
void* operator new(std::size_t bytes) {
  if (fail_new_after == 0) { fail_new_after = -1; throw std::bad_alloc(); }
  if (fail_new_after > 0) --fail_new_after;
  void* memory = std::malloc(bytes ? bytes : 1);
  if (!memory) throw std::bad_alloc();
  ++allocation_calls;
  allocation_bytes += bytes;
  return memory;
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }
static_assert(sizeof(void*) != 8 || sizeof(spv::Instruction) == 64);
static_assert(std::is_final_v<spv::Instruction> && std::is_final_v<spv::Block> &&
              std::is_final_v<spv::Function> && std::is_final_v<spv::Module>);
static_assert(!std::is_polymorphic_v<spv::Instruction> && !std::is_polymorphic_v<spv::Block> &&
              !std::is_polymorphic_v<spv::Function> && !std::is_polymorphic_v<spv::Module>);
static_assert(!std::is_copy_assignable_v<spv::Instruction> &&
              !std::is_copy_assignable_v<spv::Block> &&
              !std::is_copy_assignable_v<spv::Function> &&
              !std::is_copy_assignable_v<spv::Module>);
#endif
int main(int argc, char **argv) {
  if (argc != 3) return 1;
#if defined(XE_SPV_BINARY_CHECK) && !defined(XE_SPV_BYTE_BENCH)
  {
    bool failed = false;
    xe_spv_test_index_fail_next();
    try {
      spv::Builder rejected(spv::Spv_1_3, 0, nullptr);
      rejected.makeVoidType();
    } catch (const std::bad_alloc&) { failed = true; }
    if (!failed) return 33;
    spv::Builder recovered(spv::Spv_1_3, 0, nullptr);
    if (!recovered.makeVoidType()) return 34;
  }
#endif
#if defined(XE_SPV_BINARY_CHECK) && !defined(XE_SPV_BYTE_BENCH)
  {
    unsigned rejected = 0;
    for (int fail = 0; fail < 32; ++fail) {
      spv::Builder candidate(spv::Spv_1_3, 0, nullptr);
      candidate.makeEntryPoint("first");
      spv::Block* previous = candidate.getBuildPoint();
      spv::Block* output = previous;
      auto type = candidate.makeVoidType();
      bool failed = false;
      const size_t function_calls = allocation_calls;
      fail_new_after = fail;
      try {
        candidate.makeFunctionEntry(spv::NoPrecision, type, "candidate",
            spv::LinkageType::Max, {}, {}, &output);
      } catch (const std::bad_alloc&) { failed = true; ++rejected; }
      fail_new_after = -1;
      if (allocation_calls != function_calls) return 37;
      if (failed) {
        if (output != previous || candidate.getBuildPoint() != previous) return 35;
      } else if (output == previous || candidate.getBuildPoint() != output) return 36;
    }
    // Real function/block page failures are injected by the typed arena suite.
    if (rejected) return 37;
    std::printf("PASS: function publication needs no C++ ownership allocation\n");
  }
#endif
  const unsigned counts[] = {1, 4, 8, 33, 129, 129, 1, 1};
  const unsigned variant = unsigned(std::strtoul(argv[2], nullptr, 10));
  if (variant >= 8) return 2;
  spv::SpvBuildLogger logger;
  spv::Builder builder(spv::Spv_1_3, 0, &logger);
#ifdef XE_SPV_BINARY_CHECK
#ifndef XE_SPV_BYTE_BENCH
  // Exercise OpSourceContinued and module-process instructions as well as IR.
  builder.setEmitSpirvDebugInfo();
  builder.setSource(spv::SourceLanguage::GLSL, 450);
  builder.setDebugMainSourceFile("binary-output-test");
  builder.setSourceText(std::string(300000, 'x'));
  builder.addModuleProcessed("binary-output-regression");
#endif
#endif
  builder.addCapability(spv::Capability::Shader);
  builder.setMemoryModel(spv::AddressingModel::Logical, spv::MemoryModel::GLSL450);
  auto main = builder.makeEntryPoint("main");
  builder.addEntryPoint(spv::ExecutionModel::GLCompute, main, "main");
  builder.addExecutionMode(main, spv::ExecutionMode::LocalSize, 8, 4, 1);
  auto scalar = builder.makeFloatType(32);
  auto array = builder.makeArrayType(scalar, builder.makeUintConstant(counts[variant]), 0);
  auto local = builder.createVariable(spv::NoPrecision, spv::StorageClass::Function, array);
  std::vector<spv::Id> values;
  for (unsigned i = 0; i < counts[variant]; ++i) {
    auto a = builder.makeFloatConstant(float(i) * 0.125f);
    auto b = builder.makeFloatConstant(0.5f);
    values.push_back(builder.createBinOp(spv::Op::OpFAdd, scalar, a, b));
  }
  builder.createStore(builder.createCompositeConstruct(array, values), local);
  if (variant == 5) {
    for (unsigned block = 0; block < 512; ++block) {
      auto& next = builder.makeNewBlock();
      builder.createBranch(true, &next);
      builder.setBuildPoint(&next);
    }
  }
  builder.makeReturn(true);
  builder.leaveFunction();
  if (variant == 6) {
    for (unsigned function = 0; function < 70; ++function) {
      spv::Block* entry;
      builder.makeFunctionEntry(spv::NoPrecision, builder.makeVoidType(),
          "unused", spv::LinkageType::Max, {}, {}, &entry);
      builder.makeReturn(true);
      builder.leaveFunction();
    }
  }
  if (variant == 7) {
    for (unsigned count : {0u, 1u, 4u, 9u, 70u}) {
      std::vector<spv::Id> arguments;
      for (unsigned p = 0; p < count; ++p)
        arguments.push_back((p & 1) ? builder.makeUintType(32) : scalar);
      spv::Block* entry;
      auto* function = builder.makeFunctionEntry(spv::NoPrecision, scalar,
          "mixed_parameters", spv::LinkageType::Max, arguments, {}, &entry);
      for (unsigned p = 0; p < count; ++p)
        if (function->getParamType(p) != arguments[p] ||
            function->getParamId(p) != function->getParamId(0) + p) return 38;
      builder.makeReturn(true, count ? function->getParamId(0) : builder.makeFloatConstant(0.0f));
      builder.leaveFunction();
    }
  }
  builder.postProcess(false);
#ifdef XE_SPV_BINARY_CHECK
  size_t chain_count = 0;
  for (spv::Block* block = main->getEntryBlock(); block; block = block->getNextBlock()) {
    if (block->getTraversalIndex() != chain_count++) return 39;
  }
  if (chain_count != main->getBlockCount() || chain_count != (variant == 5 ? 513u : 1u)) return 40;
  bool callback_failed = false;
  try {
    spv::inReadableOrder(main->getEntryBlock(),
        [](spv::Block*, spv::ReachReason, spv::Block*) { throw std::bad_alloc(); });
  } catch (const std::bad_alloc&) { callback_failed = true; }
  if (!callback_failed) return 18;
#endif
  std::vector<unsigned> words;
  builder.dump(words);
#if defined(XE_SPV_BINARY_CHECK) && !defined(XE_SPV_BYTE_BENCH)
  if (words.size() * sizeof(unsigned) <= 300000) return 20;
#endif
#ifdef XE_SPV_BINARY_CHECK
  xe_spv_binary binary = {};
  for (unsigned repeat = 0; repeat < 8; ++repeat) {
    xe_spv_binary_reset(&binary);
    if (!builder.dump(binary) || binary.count != words.size() ||
        std::memcmp(binary.words, words.data(), words.size() * sizeof(unsigned))) return 5;
  }
  if (xe_spv_binary_reserve(&binary, SIZE_MAX) || !binary.failed ||
      builder.dump(binary) || binary.count) return 6;
  xe_spv_binary_reset(&binary);
  if (!builder.dump(binary) || binary.count != words.size()) return 7;
  xe_spv_binary_close(&binary);
#ifdef XE_SPV_BYTE_BENCH
  for (unsigned mode = 0; mode < 2; ++mode) {
    const size_t calls = allocation_calls + xe_bench_allocations();
    const size_t requested = allocation_bytes + xe_bench_requested();
    const uint64_t start = xe_bench_time();
    size_t total = 0, peak_owned = 0;
    for (unsigned repeat = 0; repeat < 10000; ++repeat) {
      if (mode) {
        xe::gpu::ShaderBinary result;
        if (!builder.dumpSized(*result.storage())) return 14;
        size_t owned = result.storage()->capacity * sizeof(uint32_t);
        if (owned > peak_owned) peak_owned = owned;
        total += result.size();
      } else {
        std::vector<unsigned> intermediate;
        builder.dump(intermediate);
        std::vector<uint8_t> result;
        result.reserve(intermediate.size() * sizeof(unsigned));
        const auto* begin = reinterpret_cast<const uint8_t*>(intermediate.data());
        result.insert(result.end(), begin, begin + intermediate.size() * sizeof(unsigned));
        size_t owned = intermediate.capacity() * sizeof(unsigned) + result.capacity();
        if (owned > peak_owned) peak_owned = owned;
        total += result.size();
      }
    }
    const uint64_t elapsed = xe_bench_time() - start;
    std::printf("%s modules=10000 bytes=%zu calls=%zu requested=%zu peak_output_payload=%zu ms=%.3f\n",
        mode ? "direct-owned" : "word-then-copy", total, allocation_calls + xe_bench_allocations() - calls,
        allocation_bytes + xe_bench_requested() - requested, peak_owned,
        double(elapsed) / 1000.0);
  }
#endif
  xe_spv_plan borrowed_plan = {};
  xe::gpu::ShaderBinary bytes;
  if (!builder.dumpSized(*bytes.storage(), &borrowed_plan) || bytes.size() != words.size() * sizeof(unsigned) ||
      std::memcmp(bytes.data(), words.data(), bytes.size())) return 8;
  if (borrowed_plan.count || borrowed_plan.cursor) return 30;
  const uint8_t* stable = bytes.data();
  size_t capacity = bytes.storage()->capacity;
  for (unsigned repeat = 0; repeat < 8; ++repeat) {
    if (!builder.dumpSized(*bytes.storage(), &borrowed_plan) || bytes.data() != stable || bytes.storage()->capacity != capacity ||
        bytes.size() != words.size() * sizeof(unsigned) ||
        std::memcmp(bytes.data(), words.data(), bytes.size())) return 9;
  }
  if (borrowed_plan.count || borrowed_plan.cursor) return 31;
  xe::gpu::ShaderBinary adopted(std::move(bytes));
  if (bytes.size() || adopted.data() != stable) return 15;
  bytes = std::move(adopted);
  if (adopted.size() || bytes.data() != stable) return 16;
#ifndef XE_SPV_BYTE_BENCH
  {
    xe::gpu::ShaderBinary denied;
    xe_spv_test_binary_fail_next();
    if (builder.dumpSized(*denied.storage(), &borrowed_plan) || denied.size() || !denied.storage()->failed) return 12;
    if (!builder.dumpSized(*denied.storage(), &borrowed_plan) || denied.size() != words.size() * sizeof(unsigned) ||
        std::memcmp(denied.data(), words.data(), denied.size())) return 19;
  }
#endif
  unsigned thrown_failure = 0;
#ifndef XE_SPV_BYTE_BENCH
  const size_t metadata_calls = allocation_calls;
#endif
  for (int fail = 0; fail < 32; ++fail) {
    xe::gpu::ShaderBinary partial;
    bool success = false;
    fail_new_after = fail;
    try {
      success = builder.dumpSized(*partial.storage(), &borrowed_plan);
    } catch (const std::bad_alloc&) { ++thrown_failure; }
    fail_new_after = -1;
    if (borrowed_plan.count || borrowed_plan.cursor) return 32;
    if (!success && partial.size() != 0) return 10;
    if (success && (partial.size() != words.size() * sizeof(unsigned) ||
        std::memcmp(partial.data(), words.data(), partial.size()))) return 11;
#ifndef XE_SPV_BYTE_BENCH
    if (!success) return 10;
#endif
  }
#ifndef XE_SPV_BYTE_BENCH
  if (thrown_failure || allocation_calls != metadata_calls) return 12;
  std::printf("PASS: direct owner C allocation failure/recovery and allocation-free C++ metadata emission\n");
#endif
  main->getEntryBlock()->addInstruction(builder.makeInstruction(static_cast<spv::Op>(65536)));
  if (builder.dumpSized(*bytes.storage(), &borrowed_plan) || bytes.size() != 0 ||
      borrowed_plan.count || borrowed_plan.cursor) return 13;
  xe_spv_plan_close(&borrowed_plan);
#endif
  FILE *file = std::fopen(argv[1], "wb");
  if (!file) return 3;
  bool ok = std::fwrite(words.data(), sizeof(unsigned), words.size(), file) == words.size();
  if (std::fclose(file) != 0) ok = false;
  return ok ? 0 : 4;
}
