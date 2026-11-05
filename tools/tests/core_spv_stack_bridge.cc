/* Typed lifetime/CFG bridge over stable C89 pages. No shader source parser. */
#include "SpvBuilder.h"
#include <cstdio>
#include <cstring>
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); return 1; } } while(0)
class StackBuilder : public spv::Builder {
 public:
    using spv::Builder::Builder;
    using spv::Builder::createBranch;
    using spv::Builder::createLoopMerge;
    bool values(spv::Block* a, spv::Block* b) {
        unsigned i;
        for(i=0;i<1024;++i) {
            currentDebugScopeId.push(i+1);
            switchMerges.push((i&1)?a:b);
        }
        for(i=1024;i;--i) {
            if(currentDebugScopeId.top()!=i || switchMerges.top()!=(((i-1)&1)?a:b)) return false;
            currentDebugScopeId.pop(); switchMerges.pop();
        }
        return currentDebugScopeId.empty() && switchMerges.empty();
    }
};
int main(int argc, char** argv)
{
    if(argc!=2 && argc!=3) return 1;
    StackBuilder builder(spv::Spv_1_3,0,nullptr);
    spv::Builder::LoopBlocks* references[129];
    spv::Id heads[129];
    unsigned i;
    builder.addCapability(spv::Capability::Shader);
    builder.setMemoryModel(spv::AddressingModel::Logical,spv::MemoryModel::GLSL450);
    spv::Function* main=builder.makeEntryPoint("main");
    builder.addEntryPoint(spv::ExecutionModel::GLCompute,main,"main");
    builder.addExecutionMode(main,spv::ExecutionMode::LocalSize,1,1,1);
    for(i=0;i<129;++i) {
        spv::Builder::LoopBlocks& loop=builder.makeNewLoop();
        references[i]=&loop; heads[i]=loop.head.getId();
        builder.createBranch(false,&loop.head);
        builder.setBuildPoint(&loop.head);
        builder.createLoopMerge(&loop.merge,&loop.continue_target,spv::LoopControlMask::MaskNone,{});
        builder.createBranch(false,&loop.body);
        builder.setBuildPoint(&loop.body);
    }
    for(i=0;i<129;++i) CHECK(references[i]->head.getId()==heads[i]);
    CHECK(builder.values(&references[0]->head,&references[128]->body));
    for(i=129;i;--i) {
        spv::Builder::LoopBlocks& loop=*references[i-1];
        builder.createLoopContinue();
        builder.createLoopExit();
        builder.setBuildPoint(&loop.continue_target);
        builder.createBranch(false,&loop.head);
        builder.setBuildPoint(&loop.merge);
        builder.closeLoop();
    }
    if(argc==3 && !std::strcmp(argv[2],"loop-uaf")) {
        volatile spv::Id read=references[128]->head.getId(); (void)read;
    }
    builder.makeReturn(true); builder.leaveFunction();
    xe_spv_binary binary={};
    CHECK(builder.dumpSized(binary));
    FILE* file=std::fopen(argv[1],"wb"); CHECK(file);
    int ok=std::fwrite(binary.words,sizeof(uint32_t),binary.count,file)==binary.count;
    if(std::fclose(file)) ok=0;
    xe_spv_binary_close(&binary); CHECK(ok);
    std::puts("PASS: 129 nested stable loop references, continue/exit CFG, 1024 typed ID and pointer frames");
    return 0;
}
