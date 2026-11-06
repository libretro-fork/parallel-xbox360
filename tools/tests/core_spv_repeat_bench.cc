#include "core_pipeline_bench_profile.h"
#include "xenia/gpu/spirv_builder.h"
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
static spv::Id build_repeat(xe::gpu::SpirvBuilder& builder, unsigned mode,
                           spv::Id type, spv::Id scalar, size_t count) {
    if (mode == 0) return builder.smearFloatConstant(0.5f, type);
    if (mode == 3) return builder.smearScalar(spv::NoPrecision,scalar,type);
#ifdef XE_SPV_REPEAT_LEGACY
    return builder.makeCompositeConstant(type,std::vector<spv::Id>(count,scalar));
#else
    return builder.makeCompositeConstantRepeat(type,scalar,count);
#endif
}
int main(int argc, char** argv) {
    unsigned mode, i;
    size_t count, initial, calls, requested, retained;
    uint64_t start, elapsed;
    xe_spv_binary output = {};
    if (argc != 3) return 1;
    mode = (unsigned)std::strtoul(argv[2],nullptr,10);
    if (mode > 3) return 2;
    count = mode == 1 || mode == 2 ? 1024 : 4;
    initial = xe_bench_live();
    {
        spv::InstructionArena workspace(2048);
        xe::gpu::SpirvBuilder builder(spv::Spv_1_3,0,nullptr,workspace.get());
        builder.addCapability(spv::Capability::Shader);
        builder.setMemoryModel(spv::AddressingModel::Logical,spv::MemoryModel::GLSL450);
        builder.setUseReplicatedComposites(mode == 2);
        auto* main = builder.makeEntryPoint("main");
        builder.addEntryPoint(spv::ExecutionModel::GLCompute,main,"main");
        builder.addExecutionMode(main,spv::ExecutionMode::LocalSize,1,1,1);
        spv::Id scalarType = builder.makeFloatType(32);
        spv::Id scalar = builder.makeFloatConstant(0.5f);
        spv::Id type = count == 4 ? builder.makeVectorType(scalarType,4) :
            builder.makeArrayType(scalarType,builder.makeUintConstant((unsigned)count),0);
        if (mode == 3) builder.setToSpecConstCodeGenMode();
        spv::Id expected = build_repeat(builder,mode,type,scalar,count);
        retained = xe_bench_live() - initial;
        calls = xe_bench_allocations(); requested = xe_bench_requested(); start = xe_bench_time();
        for (i = 0; i < 100000; ++i)
            if (build_repeat(builder,mode,type,scalar,count) != expected) return 3;
        elapsed = xe_bench_time() - start;
        if (xe_bench_live() - initial != retained) return 4;
        std::printf("mode=%u lookups=100000 calls=%zu requested=%zu retained=%zu ms=%.3f\n",
            mode,xe_bench_allocations()-calls,xe_bench_requested()-requested,retained,double(elapsed)/1000.0);
        builder.setToNormalCodeGenMode(); builder.makeReturn(true); builder.leaveFunction();
        if (!builder.dumpSized(output)) return 5;
    }
    FILE* file = std::fopen(argv[1],"wb"); if(!file) return 6;
    bool ok = std::fwrite(output.words,sizeof(uint32_t),output.count,file)==output.count;
    if(std::fclose(file)) ok = false;
    xe_spv_binary_close(&output);
    return ok && xe_bench_live() == initial ? 0 : 7;
}
