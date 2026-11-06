/* Exercise production postprocessing, not just the C89 query helpers. */
#include "SpvBuilder.h"
#include <cstdio>
#include <cstring>
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"query line %d: %s\n",__LINE__,#x); return 1; } } while(0)
int main(int argc,char** argv) {
    if(argc!=3) return 1;
    spv::Builder builder(spv::Spv_1_3,0,nullptr);
    builder.addCapability(spv::Capability::Shader);
    builder.addCapability(spv::Capability::PhysicalStorageBufferAddresses);
    builder.addCapability(spv::Capability::Int64);
    builder.addExtension("SPV_KHR_physical_storage_buffer");
    builder.setMemoryModel(spv::AddressingModel::PhysicalStorageBuffer64,spv::MemoryModel::GLSL450);
    auto* main=builder.makeEntryPoint("main"); builder.addEntryPoint(spv::ExecutionModel::GLCompute,main,"main");
    builder.addExecutionMode(main,spv::ExecutionMode::LocalSize,1,1,1);
    spv::Id f32=builder.makeFloatType(32);
    spv::Id structure=builder.makeStructType({f32},"record");
    builder.addMemberDecoration(structure,0,spv::Decoration::Offset,16);
    spv::Id array=builder.makeArrayType(structure,builder.makeUintConstant(4),64);
    builder.addDecoration(array,spv::Decoration::ArrayStride,64);
    spv::Id pointer=builder.makePointer(spv::StorageClass::PhysicalStorageBuffer,array);
    spv::Id base=builder.createUnaryOp(spv::Op::OpConvertUToPtr,pointer,builder.makeUint64Constant(4096));
    spv::Id element=builder.createAccessChain(spv::StorageClass::PhysicalStorageBuffer,base,
        {builder.makeUintConstant(1),builder.makeUintConstant(0)});
    spv::Id load=builder.createLoad(element,spv::NoPrecision,spv::MemoryAccessMask::Aligned,spv::Scope::Max,128);
    spv::Id physical=builder.makePointer(spv::StorageClass::PhysicalStorageBuffer,f32);
    spv::Id locals[3];
    for(unsigned i=0;i<3;++i) locals[i]=builder.createVariable(spv::NoPrecision,spv::StorageClass::Function,physical);
    builder.addDecoration(locals[1],spv::Decoration::RestrictPointerEXT);
    builder.addDecoration(locals[2],spv::Decoration::AliasedPointerEXT);
    builder.makeReturn(true); builder.leaveFunction();
    builder.postProcessFeatures(); builder.postProcessFeatures();
    xe_spv_binary binary={}; CHECK(builder.dumpSized(binary));
    unsigned seenLoad=0, alias[3]={}, restricts[3]={};
    for(size_t offset=5;offset<binary.count;) {
        const uint32_t* w=binary.words+offset; size_t count=w[0]>>16;
        CHECK(count && count<=binary.count-offset);
        if((w[0]&65535)==(unsigned)spv::Op::OpLoad && count>=6 && w[2]==load) { CHECK(w[5]==16); ++seenLoad; }
        if((w[0]&65535)==(unsigned)spv::Op::OpDecorate && count==3) for(unsigned i=0;i<3;++i) if(w[1]==locals[i]) {
            if(w[2]==(unsigned)spv::Decoration::AliasedPointerEXT) ++alias[i];
            if(w[2]==(unsigned)spv::Decoration::RestrictPointerEXT) ++restricts[i];
        }
        offset+=count;
    }
    CHECK(seenLoad==1 && alias[0]==1 && !restricts[0] && !alias[1] && restricts[1]==1 && alias[2]==1 && !restricts[2]);
    FILE* file=std::fopen(argv[1],"wb"); CHECK(file);
    CHECK(std::fwrite(binary.words,4,binary.count,file)==binary.count); CHECK(!std::fclose(file));
    char last[2048]; CHECK(std::snprintf(last,sizeof(last),"%s.last.spv",argv[1])>0);
    file=std::fopen(last,"wb"); CHECK(file); CHECK(std::fwrite(binary.words,4,binary.count,file)==binary.count); CHECK(!std::fclose(file));
    xe_spv_binary_close(&binary);
    std::puts("PASS: physical array/member alignment and explicit/default pointer alias decorations across repeated feature passes");
    return 0;
}
