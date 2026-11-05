/* Raw probe versus production builder and capability wire/postprocess checks. */
#include "SpvBuilder.h"
#include "libretro/core_spv_probe.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); return 1; } } while(0)
static int has_cap(const std::vector<unsigned>& words, spv::Capability cap) {
    size_t offset;
    for(offset=5;offset<words.size();) {
        size_t count=words[offset]>>16;
        if(!count || count>words.size()-offset) return -1;
        if((words[offset]&65535)==(unsigned)spv::Op::OpCapability && count==2 && words[offset+1]==(unsigned)cap) return 1;
        offset+=count;
    }
    return 0;
}
int main(int argc, char** argv)
{
    if(argc!=3) return 1;
    unsigned variant=(unsigned)std::strtoul(argv[2],nullptr,10);
    FILE* file=std::fopen(argv[1],"wb"); CHECK(file);
    if(variant==3) {
        for(unsigned minor=0;minor<=6;++minor) {
            unsigned version=0x10000|(minor<<8);
            spv::Builder builder(version,26,nullptr);
            builder.setSource(spv::SourceLanguage::Unknown,0);
            builder.setMemoryModel(spv::AddressingModel::Logical,spv::MemoryModel::GLSL450);
            builder.addCapability(spv::Capability::Shader);
            spv::Block* entry;
            spv::Function* function=builder.makeFunctionEntry(spv::NoPrecision,builder.makeVoidType(),"main",spv::LinkageType::Max,{},{},&entry);
            builder.addEntryPoint(spv::ExecutionModel::GLCompute,function,"main");
            builder.addExecutionMode(function,spv::ExecutionMode::LocalSize,1,1,1);
            builder.makeReturn(false); builder.leaveFunction();
            std::vector<unsigned> reference; builder.dump(reference);
            uint32_t direct[XE_SPV_COMPUTE_PROBE_WORDS];
            size_t count=xe_spv_compute_probe(direct,XE_SPV_COMPUTE_PROBE_WORDS,version,26);
            CHECK(count==reference.size() && !std::memcmp(direct,reference.data(),count*sizeof(uint32_t)));
            CHECK(std::fwrite(direct,sizeof(uint32_t),count,file)==count);
        }
    } else {
        spv::Builder builder(spv::Spv_1_3,0,nullptr);
        if(!variant) {
            for(unsigned i=130;i;--i) builder.addCapability(static_cast<spv::Capability>(i-1));
            builder.addCapability(static_cast<spv::Capability>(UINT32_MAX));
            builder.addCapability(static_cast<spv::Capability>(0x80000000));
            builder.addCapability(static_cast<spv::Capability>(0x7fffffff));
            builder.addCapability(spv::Capability::Matrix);
        } else {
            builder.addCapability(spv::Capability::Shader);
            builder.addCapability(spv::Capability::Shader);
            builder.addCapability(spv::Capability::VulkanMemoryModelKHR);
            if(variant==1) {
                builder.addCapability(spv::Capability::StoragePushConstant16);
                builder.addCapability(spv::Capability::StoragePushConstant8);
            }
            auto* main=builder.makeEntryPoint("main");
            builder.addEntryPoint(spv::ExecutionModel::GLCompute,main,"main");
            builder.addExecutionMode(main,spv::ExecutionMode::LocalSize,1,1,1);
            builder.createUnaryOp(spv::Op::OpFConvert,builder.makeFloatType(16),builder.makeFloatConstant(1.0f));
            builder.createUnaryOp(spv::Op::OpSConvert,builder.makeIntType(8),builder.makeIntConstant(1));
            builder.makeReturn(true); builder.leaveFunction();
            builder.postProcess(false);
        }
        std::vector<unsigned> words; builder.dump(words);
        if(variant) {
            CHECK(has_cap(words,spv::Capability::Float16)==(variant==2));
            CHECK(has_cap(words,spv::Capability::Int8)==(variant==2));
        }
        size_t offset=5, seen=0; unsigned previous=0;
        while(offset<words.size() && (words[offset]&65535)==(unsigned)spv::Op::OpCapability) {
            CHECK((words[offset]>>16)==2 && (!seen || previous<words[offset+1]));
            previous=words[offset+1]; ++seen; offset+=2;
        }
        CHECK(variant || seen==133);
        CHECK(std::fwrite(words.data(),sizeof(unsigned),words.size(),file)==words.size());
    }
    CHECK(!std::fclose(file));
    std::puts("PASS: capability ordering/dedup/storage inference and direct probe byte equivalence");
    return 0;
}
