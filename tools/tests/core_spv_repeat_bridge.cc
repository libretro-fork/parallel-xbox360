/* Typed producer oracle; parent builds its real vector path. */
#include "xenia/gpu/spirv_builder.h"
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); return 1; } } while(0)
static spv::Id repeated(xe::gpu::SpirvBuilder& builder, spv::Id type,
                       spv::Id member, size_t count, bool spec = false) {
#ifdef XE_SPV_REPEAT_LEGACY
    return builder.makeCompositeConstant(type, std::vector<spv::Id>(count, member), spec);
#else
    return builder.makeCompositeConstantRepeat(type, member, count, spec);
#endif
}
int main(int argc, char** argv) {
    if (argc != 3) return 1;
    const unsigned variant = unsigned(std::atoi(argv[2]));
    xe::gpu::SpirvBuilder builder(spv::Spv_1_3, 0, nullptr);
    builder.setUseReplicatedComposites((variant & 1) != 0);
    builder.addCapability(spv::Capability::Shader);
    builder.setMemoryModel(spv::AddressingModel::Logical, spv::MemoryModel::GLSL450);
    auto* main = builder.makeEntryPoint("main");
    builder.addEntryPoint(spv::ExecutionModel::GLCompute, main, "main");
    builder.addExecutionMode(main, spv::ExecutionMode::LocalSize, 1, 1, 1);
    spv::Id scalar = builder.makeFloatType(32), value = builder.makeFloatConstant(-0.0f);
    const unsigned counts[] = {2,3,4,16,1024};
    for (unsigned count : counts) {
        spv::Id type = count <= 4 ? builder.makeVectorType(scalar, count) :
            builder.makeArrayType(scalar, builder.makeUintConstant(count), 0);
        std::vector<spv::Id> words(count, value);
        spv::Id first = repeated(builder, type, value, count);
        CHECK(first == builder.makeCompositeConstantView(type, xe_spv_words_view(words.data(), count)));
        CHECK(first == repeated(builder, type, value, count));
        spv::Id other = builder.makeFloatConstant(1.25f);
        words.assign(count, other);
        spv::Id normalFirst = builder.makeCompositeConstantView(type, xe_spv_words_view(words.data(), count));
        CHECK(normalFirst == repeated(builder, type, other, count));
        words.assign(count, value); // cached packet must not retain caller storage
        CHECK(normalFirst == repeated(builder, type, other, count));
        spv::Id specialized = repeated(builder, type, value, count, true);
        CHECK(specialized != first && specialized != repeated(builder, type, value, count, true));
        if (count <= 4) CHECK(first == builder.smearFloatConstant(-0.0f, type));
    }
    spv::Id fields[2] = {scalar,scalar};
    spv::Id structure = builder.makeStructTypeView(xe_spv_words_view(fields,2),"pair");
    spv::Id pair = repeated(builder,structure,value,2);
    CHECK(pair == repeated(builder,structure,value,2));
    CHECK(pair != repeated(builder,structure,value,2,true));
    spv::Id empty = builder.makeStructTypeView(xe_spv_words_view(nullptr,0),"empty");
    CHECK(repeated(builder,empty,value,0) == builder.makeCompositeConstantView(empty,xe_spv_words_view(nullptr,0)));
    spv::Id vector = builder.makeVectorType(scalar,4);
#ifndef XE_SPV_REPEAT_LEGACY
    if (!(variant & 1)) {
        bool failed = false;
        try { builder.makeCompositeConstantRepeat(vector,value,SIZE_MAX); }
        catch (const std::bad_alloc&) { failed = true; }
        CHECK(failed);
    }
#endif
    spv::Id column = builder.makeVectorType(scalar,2);
    spv::Id columnValue = repeated(builder,column,value,2);
    repeated(builder,builder.makeMatrixType(scalar,2,2),columnValue,2);
    if (variant >= 2) {
        spv::Id cooperative = builder.makeCooperativeVectorTypeNV(scalar,builder.makeUintConstant(1024));
        CHECK(repeated(builder,cooperative,value,1024) == repeated(builder,cooperative,value,1024));
    }
    builder.setToSpecConstCodeGenMode();
    builder.smearScalar(spv::NoPrecision,value,vector);
    spv::Id specValue = builder.makeFloatConstant(0.5f,true);
    builder.smearScalar(spv::NoPrecision,specValue,vector);
    builder.setToNormalCodeGenMode();
    spv::Id array = builder.makeArrayType(vector,builder.makeUintConstant(2),0);
    spv::Id vecValue = repeated(builder,vector,value,4);
    spv::Id aggregate = repeated(builder,array,vecValue,2);
    builder.createCompositeCompare(spv::NoPrecision,aggregate,aggregate,true);
    builder.setToSpecConstCodeGenMode();
    builder.createCompositeExtract(vecValue,scalar,2u);
    builder.createCompositeExtract(aggregate,scalar,std::vector<unsigned>{1,2});
    builder.setToNormalCodeGenMode();
    builder.makeReturn(true); builder.leaveFunction();
    xe_spv_binary out = {};
    CHECK(builder.dumpSized(out));
    FILE* file = std::fopen(argv[1],"wb"); CHECK(file);
    CHECK(std::fwrite(out.words,sizeof(uint32_t),out.count,file)==out.count);
    CHECK(!std::fclose(file)); xe_spv_binary_close(&out);
    std::puts("PASS: repeated/view cache identity, scalar smears, structs, empty/large sources, specialization and extract/compare output");
    return 0;
}
