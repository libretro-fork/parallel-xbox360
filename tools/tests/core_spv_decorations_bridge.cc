/* Production API oracle. Arbitrary decorator forms are byte cases, not shaders. */
#include "SpvBuilder.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); return 1; } } while(0)
static bool snapshot(spv::Builder& builder, FILE* file, const char* last) {
    xe_spv_binary binary = {}; std::vector<unsigned> vector;
    builder.dump(vector);
    if (!builder.dumpSized(binary) || vector.size()!=binary.count ||
        std::memcmp(vector.data(),binary.words,binary.count*4)) return false;
    bool ok = std::fwrite(binary.words,4,binary.count,file)==binary.count;
    if (last) {
        FILE* output=std::fopen(last,"wb"); if(!output) ok=false;
        else { if(std::fwrite(binary.words,4,binary.count,output)!=binary.count) ok=false; if(std::fclose(output)) ok=false; }
    }
    xe_spv_binary_close(&binary); return ok;
}
int main(int argc,char** argv) {
    if(argc!=3) return 1;
    const bool arbitrary=std::atoi(argv[2])!=0;
    spv::Builder builder(spv::Spv_1_3,0,nullptr);
    builder.addCapability(spv::Capability::Shader);
    builder.setMemoryModel(spv::AddressingModel::Logical,spv::MemoryModel::GLSL450);
    auto* main=builder.makeEntryPoint("main");
    builder.addEntryPoint(spv::ExecutionModel::GLCompute,main,"main");
    builder.addExecutionMode(main,spv::ExecutionMode::LocalSize,1,1,1);
    spv::Id f32=builder.makeFloatType(32), value=builder.makeFloatConstant(0.5f);
    builder.addDecoration(value,spv::Decoration::RelaxedPrecision);
    builder.makeReturn(true);
    auto& dead=builder.makeNewBlock(); builder.setBuildPoint(&dead);
    spv::Id deadValue=builder.createUnaryOp(spv::Op::OpCopyObject,f32,value);
    builder.addDecoration(deadValue,spv::Decoration::RelaxedPrecision);
    builder.makeReturn(true); builder.leaveFunction();
    FILE* output=std::fopen(argv[1],"wb"); CHECK(output);
    CHECK(snapshot(builder,output,nullptr));
    for(unsigned i=0;i<10000;++i) builder.addDecoration(value,spv::Decoration::RelaxedPrecision);
    builder.addDecoration(value,spv::Decoration::Max,(const char*)nullptr);
    CHECK(snapshot(builder,output,nullptr));
    builder.postProcessCFG();
    CHECK(snapshot(builder,output,nullptr));
    if(arbitrary) {
        char text[300]; const char* texts[3]={"one","", "high_\x80\xff"};
        std::vector<unsigned> literals(70), ids(70);
        for(unsigned i=193;i;--i) {
            unsigned target=1000+i%17;
            for(unsigned j=0;j<70;++j) { literals[j]=(i*71+j)^0x80000000u; ids[j]=i*31+j; }
            std::memset(text,'A'+i%26,sizeof(text)); text[i%129]=0;
            builder.addDecoration(target,spv::Decoration::Location,int(i));
            builder.addDecoration(target,spv::Decoration::Location,literals);
            builder.addDecoration(target,spv::Decoration::UserSemantic,text);
            builder.addDecoration(target,spv::Decoration::UserSemantic,std::vector<const char*>(texts,texts+3));
            builder.addDecorationId(target,spv::Decoration::AlignmentId,i);
            builder.addDecorationId(target,spv::Decoration::AlignmentId,ids);
            builder.addMemberDecoration(target,i%5,spv::Decoration::Offset,int(i));
            builder.addMemberDecoration(target,i%5,spv::Decoration::Offset,literals);
            builder.addMemberDecoration(target,i%5,spv::Decoration::UserSemantic,text);
            builder.addMemberDecoration(target,i%5,spv::Decoration::UserSemantic,std::vector<const char*>(texts,texts+3));
            // Counted prefixes may contain NUL and are not C-string lengths.
            text[2]=0; builder.addLinkageDecoration(target,text,128,spv::LinkageType::Import);
            builder.addLinkageDecoration(target,text,128,spv::LinkageType::Import);
            std::memset(text,'X',sizeof(text));
        }
        builder.addLinkageDecoration(9000,nullptr,0,spv::LinkageType::Export);
        builder.addDecoration(9001,spv::Decoration::Location,std::vector<unsigned>());
        builder.addDecoration(9001,spv::Decoration::Location);
        builder.addDecorationId(9002,spv::Decoration::AlignmentId,std::vector<spv::Id>());
        builder.addMemberDecoration(9003,4,spv::Decoration::UserSemantic,std::vector<const char*>());
        CHECK(snapshot(builder,output,nullptr)); builder.postProcessCFG();
    }
    char last[2048]; CHECK(std::snprintf(last,sizeof(last),"%s.last.spv",argv[1])>0);
    CHECK(snapshot(builder,output,last)); CHECK(!std::fclose(output));
    std::puts("PASS: eleven decoration factories, repeated keys, owned strings/values, counted text, sorted output and unreachable pruning");
    return 0;
}
