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


class WalkBuilder : public spv::Builder {
 public: using spv::Builder::Builder; using spv::Builder::createBranch;
};
int main(int argc,char** argv) {
    if(argc!=3) return 1;
    unsigned mode=(unsigned)std::atoi(argv[2]); if(mode>3) return 2;
    const unsigned blocks=mode==1?64:mode==2?513:1;
    const unsigned functions=mode==3?257:1;
    const unsigned repetitions=mode==0?100000:mode==1?10000:1000;
    size_t initial=xe_bench_live(), calls, bytes, retained;
    uint64_t start, elapsed;
    xe_spv_binary output={}; xe_spv_plan plan={};
    {
        spv::InstructionArena workspace(2048);
        WalkBuilder builder(spv::Spv_1_3,0,nullptr,workspace.get());
        builder.addCapability(spv::Capability::Shader);
        builder.setMemoryModel(spv::AddressingModel::Logical,spv::MemoryModel::GLSL450);
        auto* main=builder.makeEntryPoint("main"); builder.addEntryPoint(spv::ExecutionModel::GLCompute,main,"main");
        builder.addExecutionMode(main,spv::ExecutionMode::LocalSize,1,1,1);
        for(unsigned i=1;i<blocks;++i) { auto* next=&builder.makeNewBlock(); builder.createBranch(false,next); builder.setBuildPoint(next); }
        builder.makeReturn(true); builder.leaveFunction();
        for(unsigned i=1;i<functions;++i) {
            spv::Block* entry;
            builder.makeFunctionEntry(spv::NoPrecision,builder.makeVoidType(),"helper",spv::LinkageType::Max,{},{},&entry);
            builder.makeReturn(true); builder.leaveFunction();
        }
        if(!builder.dumpSized(output,&plan)) return 3;
        const size_t expected=output.count;
        retained=xe_bench_live()-initial; calls=xe_bench_allocations(); bytes=xe_bench_requested(); start=xe_bench_time();
        for(unsigned i=0;i<repetitions;++i) if(!builder.dumpSized(output,&plan) || output.count!=expected) return 4;
        elapsed=xe_bench_time()-start;
        if(xe_bench_live()-initial!=retained) return 5;
        std::printf("mode=%u modules=%u blocks=%u functions=%u calls=%zu requested=%zu retained=%zu ms=%.3f\n",
            mode,repetitions,blocks,functions,xe_bench_allocations()-calls,xe_bench_requested()-bytes,retained,double(elapsed)/1000.0);
    }
    FILE* file=std::fopen(argv[1],"wb"); if(!file) return 6;
    bool ok=std::fwrite(output.words,4,output.count,file)==output.count; if(std::fclose(file)) ok=false;
    xe_spv_plan_close(&plan); xe_spv_binary_close(&output);
    return ok && xe_bench_live()==initial?0:7;
}
