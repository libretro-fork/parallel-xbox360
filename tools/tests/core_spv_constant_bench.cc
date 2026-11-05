/* Production typed builder only; workload/accounting loops are C89-shaped. */
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
static void build_constants(xe_spv_arena* arena, unsigned count, xe_spv_binary* output)
{
    spv::Builder builder(spv::Spv_1_3, 0, nullptr, arena);
    spv::Id integers[2048], floats[2048], composites[1024];
    unsigned i, pass;
    builder.addCapability(spv::Capability::Shader);
    builder.setMemoryModel(spv::AddressingModel::Logical, spv::MemoryModel::GLSL450);
    spv::Function* main = builder.makeEntryPoint("main");
    builder.addEntryPoint(spv::ExecutionModel::GLCompute, main, "main");
    builder.addExecutionMode(main, spv::ExecutionMode::LocalSize, 1, 1, 1);
    spv::Id vector = builder.makeVectorType(builder.makeUintType(32), 2);
    for (i = 0; i < count; ++i) {
        integers[i] = builder.makeUintConstant(i);
        floats[i] = builder.makeFloatConstant((float)i * 0.25f);
    }
    for (i = 0; i < count / 2; ++i)
        composites[i] = builder.makeCompositeConstantView(vector, xe_spv_words_view(integers + i * 2, 2));
    for (pass = 0; pass < 3; ++pass) {
        for (i = 0; i < count; ++i) {
            if (integers[i] != builder.makeUintConstant(i) ||
                floats[i] != builder.makeFloatConstant((float)i * 0.25f)) std::abort();
        }
        for (i = 0; i < count / 2; ++i)
            if (composites[i] != builder.makeCompositeConstantView(vector, xe_spv_words_view(integers + i * 2, 2))) std::abort();
    }
    builder.makeReturn(true);
    builder.leaveFunction();
    if (output && !builder.dumpSized(*output)) std::abort();
}
int main(int argc, char** argv)
{
    if (argc != 3) return 1;
    unsigned variant = (unsigned)std::strtoul(argv[2], nullptr, 10);
    unsigned count = variant == 0 ? 16 : variant == 4 ? 256 : variant == 5 ? 2048 : 0;
    unsigned repeat, modules = count == 2048 ? 100 : 1000;
    if (!count) return 2;
    size_t initial = xe_bench_live();
    xe_spv_binary output = {};
    {
        spv::InstructionArena workspace(count * 4 + 64);
        build_constants(workspace.get(), count, &output);
        size_t warmed = xe_bench_live(), calls = xe_bench_allocations(), requested = xe_bench_requested();
        uint64_t start = xe_bench_time();
        for (repeat = 0; repeat < modules; ++repeat) build_constants(workspace.get(), count, nullptr);
        uint64_t elapsed = xe_bench_time() - start;
        if (xe_bench_live() != warmed) return 3;
        std::printf("constants=%u modules=%u builder_bytes=%zu calls=%zu requested=%zu retained=%zu peak=%zu ms=%.3f\n",
            count * 5 / 2, modules, sizeof(spv::Builder), xe_bench_allocations() - calls,
            xe_bench_requested() - requested, warmed - initial, xe_bench_peak() - initial, (double)elapsed / 1000.0);
    }
    FILE* file = std::fopen(argv[1], "wb");
    if (!file) return 4;
    int ok = std::fwrite(output.words, sizeof(uint32_t), output.count, file) == output.count;
    if (std::fclose(file)) ok = 0;
    xe_spv_binary_close(&output);
    if (!ok || xe_bench_live() != initial) return 5;
    return 0;
}
