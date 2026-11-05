/* Typed identity/lifetime interop. The registry never determines type equality. */
#include "SpvBuilder.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); return 1; } } while(0)
int main(int argc, char** argv)
{
    if (argc != 3) return 1;
    spv::Builder builder(spv::Spv_1_3,0,nullptr);
    if (std::atoi(argv[2])) builder.setEmitNonSemanticShaderDebugInfo(false);
    builder.addCapability(spv::Capability::Shader);
    builder.setMemoryModel(spv::AddressingModel::Logical,spv::MemoryModel::GLSL450);
    CHECK(!builder.isBoolType(0));
    spv::Id boolean=builder.makeBoolType();
    CHECK(builder.isBoolType(boolean) && boolean==builder.makeBoolType());
    spv::Id u32=builder.makeUintType(32), s32=builder.makeIntType(32);
    CHECK(u32==builder.makeUintType(32) && u32!=s32);
    CHECK(builder.makeVoidType()==builder.makeVoidType());
    CHECK(builder.makeSamplerType()==builder.makeSamplerType());
    spv::Id vector=builder.makeVectorType(u32,4);
    CHECK(vector==builder.makeVectorType(u32,4));
    spv::Id size=builder.makeUintConstant(8);
    spv::Id plain=builder.makeArrayType(u32,size,0);
    spv::Id laid=builder.makeArrayType(u32,size,16);
    CHECK(plain!=laid && plain==builder.makeArrayType(u32,size,0));
    CHECK(laid!=builder.makeArrayType(u32,size,16));
    spv::Id fields[2]={u32,s32};
    spv::Id first=builder.makeStructTypeView(xe_spv_words_view(fields,2),"first");
    spv::Id second=builder.makeStructTypeView(xe_spv_words_view(fields,2),"second");
    CHECK(first!=second && first==builder.makeStructResultType(u32,s32));
    spv::Id forward=builder.makeForwardPointer(spv::StorageClass::Function);
    spv::Id pointer=builder.makePointerFromForwardPointer(spv::StorageClass::Function,forward,u32);
    CHECK(pointer==forward && pointer==builder.makePointer(spv::StorageClass::Function,u32));
    CHECK(pointer!=builder.makePointer(spv::StorageClass::Private,u32));
    CHECK(builder.makeAccelerationStructureType()==builder.makeAccelerationStructureType());
    CHECK(builder.makeRayQueryType()==builder.makeRayQueryType());
    CHECK(builder.makeHitObjectNVType()==builder.makeHitObjectNVType());
    xe_spv_binary binary={};
    CHECK(builder.dumpSized(binary));
    FILE* file=std::fopen(argv[1],"wb");
    CHECK(file);
    int ok=std::fwrite(binary.words,sizeof(uint32_t),binary.count,file)==binary.count;
    if(std::fclose(file)) ok=0;
    xe_spv_binary_close(&binary);
    CHECK(ok);
    std::puts("PASS: type identities, laid-out arrays, distinct structs, forward-pointer patching and debug groups");
    return 0;
}
