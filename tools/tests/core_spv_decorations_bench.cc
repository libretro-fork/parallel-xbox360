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

static void decorate(spv::Builder& builder,unsigned mode,spv::Id target,const std::vector<spv::Id>& values,const char* text) {
    if(mode==0) builder.addDecoration(target,spv::Decoration::Location,7);
    else if(mode==1) builder.addDecorationId(target,spv::Decoration::AlignmentId,
        values);
    else builder.addLinkageDecoration(target,text,128,spv::LinkageType::Import);
}
static void module(xe_spv_arena* arena,xe_spv_binary* output,unsigned order) {
    spv::Builder builder(spv::Spv_1_3,0,nullptr,arena);
    builder.addCapability(spv::Capability::Shader);
    builder.setMemoryModel(spv::AddressingModel::Logical,spv::MemoryModel::GLSL450);
    auto* main=builder.makeEntryPoint("main"); builder.addEntryPoint(spv::ExecutionModel::GLCompute,main,"main");
    builder.addExecutionMode(main,spv::ExecutionMode::LocalSize,1,1,1);
    spv::Id targets[512];
    for(unsigned i=0;i<512;++i) targets[i]=builder.makeFloatConstant(float(i));
    for(unsigned i=0;i<512;++i) {
        unsigned index=order==4?511-i:order==5?(i*149)&511:i;
        builder.addDecoration(targets[index],spv::Decoration::RelaxedPrecision);
    }
    builder.makeReturn(true); builder.leaveFunction();
    if(output && !builder.dumpSized(*output)) std::abort();
}
int main(int argc,char** argv) {
    if(argc!=3) return 1;
    unsigned mode=(unsigned)std::atoi(argv[2]); if(mode>5) return 2;
    size_t initial=xe_bench_live(), before, requested, retained, allocations, bytes, after;
    uint64_t start, elapsed;
    xe_spv_binary output={};
    {
        spv::InstructionArena workspace(2048);
        if(mode>=3) {
            module(workspace.get(),&output,mode);
            retained=xe_bench_live()-initial; before=xe_bench_allocations(); requested=xe_bench_requested(); start=xe_bench_time();
            for(unsigned i=0;i<1000;++i) module(workspace.get(),nullptr,mode);
            elapsed=xe_bench_time()-start;
            allocations=xe_bench_allocations()-before; bytes=xe_bench_requested()-requested; after=xe_bench_live()-initial;
        } else {
            spv::Builder builder(spv::Spv_1_3,0,nullptr,workspace.get());
            builder.setMemoryModel(spv::AddressingModel::Logical,spv::MemoryModel::GLSL450);
            std::vector<spv::Id> values(128); char text[128];
            for(unsigned i=0;i<128;++i) { values[i]=i+1; text[i]=char(i); }
            decorate(builder,mode,2000,values,text);
            retained=xe_bench_live()-initial; before=xe_bench_allocations(); requested=xe_bench_requested(); start=xe_bench_time();
            for(unsigned i=0;i<100000;++i) decorate(builder,mode,2000,values,text);
            elapsed=xe_bench_time()-start;
            allocations=xe_bench_allocations()-before; bytes=xe_bench_requested()-requested; after=xe_bench_live()-initial;
            if(!builder.dumpSized(output)) return 3;
        }
        std::printf("mode=%u iterations=%u builder_bytes=%zu calls=%zu requested=%zu retained_before=%zu retained_after=%zu ms=%.3f\n",
            mode,mode>=3?1000:100000,sizeof(spv::Builder),allocations,
            bytes,retained,after,double(elapsed)/1000.0);
    }
    FILE* file=std::fopen(argv[1],"wb"); if(!file) return 4;
    bool ok=std::fwrite(output.words,4,output.count,file)==output.count;
    if(std::fclose(file)) ok=false;
    xe_spv_binary_close(&output); return ok && xe_bench_live()==initial?0:5;
}
