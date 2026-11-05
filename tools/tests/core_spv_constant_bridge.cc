/* Typed production-builder interop; exact constant keys, no driver or GLSL. */
#include "SpvBuilder.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main(int argc, char** argv)
{
    if (argc != 3) return 1;
    spv::Builder builder(spv::Spv_1_3, 0, nullptr);
    builder.addCapability(spv::Capability::Shader);
    builder.setMemoryModel(spv::AddressingModel::Logical, spv::MemoryModel::GLSL450);
    spv::Function* main = builder.makeEntryPoint("main");
    builder.addEntryPoint(spv::ExecutionModel::GLCompute, main, "main");
    builder.addExecutionMode(main, spv::ExecutionMode::LocalSize, 1, 1, 1);
    spv::Id u32 = builder.makeUintType(32), s32 = builder.makeIntType(32);
    spv::Id u64 = builder.makeUintType(64);
    spv::Id small = builder.makeIntConstant(u32, 123, false);
    CHECK(small == builder.makeIntConstant(u32, 123, false));
    CHECK(small != builder.makeIntConstant(s32, 123, false));
    CHECK(small != builder.makeIntConstant(u32, 123, true));
    CHECK(builder.makeIntConstant(u32, 123, true) != builder.makeIntConstant(u32, 123, true));
    spv::Id low64 = builder.makeInt64Constant(u64, 123, false);
    spv::Id high64 = builder.makeInt64Constant(u64, 0x10000007bULL, false);
    CHECK(low64 != high64);
    spv::Id trueId = builder.makeBoolConstant(true);
    spv::Id falseId = builder.makeBoolConstant(false);
    CHECK(trueId != falseId);
    CHECK(builder.makeBoolConstant(true, true) != builder.makeBoolConstant(true, true));
    spv::Id positiveZero = builder.makeFloatConstant(0.0f);
    CHECK(positiveZero != builder.makeFloatConstant(-0.0f));
    uint32_t nanBits[2] = {0x7fc00001, 0x7fc00002};
    float nan[2];
    std::memcpy(nan, nanBits, sizeof(nan));
    spv::Id firstNan = builder.makeFloatConstant(nan[0]);
    spv::Id secondNan = builder.makeFloatConstant(nan[1]);
    CHECK(firstNan != secondNan);
    CHECK(builder.makeFloatConstant(nan[0]) == builder.makeFloatConstant(nan[0]));
    spv::Id vectorType = builder.makeVectorType(u32, 2);
    spv::Id members[2] = {small, builder.makeUintConstant(456)};
    spv::Id vector = builder.makeCompositeConstantView(vectorType, xe_spv_words_view(members, 2));
    CHECK(vector == builder.makeCompositeConstantView(vectorType, xe_spv_words_view(members, 2)));
    spv::Id reversed[2] = {members[1], members[0]};
    CHECK(vector != builder.makeCompositeConstantView(vectorType, xe_spv_words_view(reversed, 2)));
    spv::Id fields[2] = {u32, u32};
    spv::Id structure = builder.makeStructTypeView(xe_spv_words_view(fields, 2), "structure");
    CHECK(builder.makeNullConstant(structure) == builder.makeNullConstant(structure));
    CHECK(builder.makeNullConstant(structure) != builder.makeNullConstant(vectorType));
    CHECK(builder.makeCompositeConstantView(structure, xe_spv_words_view(members, 2)) ==
        builder.makeCompositeConstantView(structure, xe_spv_words_view(members, 2)));
    if (std::atoi(argv[2])) {
        spv::Id separateFields[2] = {s32, s32};
        spv::Id separateStruct = builder.makeStructTypeView(xe_spv_words_view(separateFields, 2), "specialized_structure");
        spv::Id signedMembers[2] = {builder.makeIntConstant(s32, 34, false), builder.makeIntConstant(s32, 56, false)};
        spv::Id specialized = builder.makeCompositeConstantView(separateStruct, xe_spv_words_view(signedMembers, 2), true);
        spv::Id ordinary = builder.makeCompositeConstantView(separateStruct, xe_spv_words_view(signedMembers, 2), false);
        CHECK(ordinary != specialized);
        CHECK(ordinary == builder.makeCompositeConstantView(separateStruct, xe_spv_words_view(signedMembers, 2), false));
        CHECK(specialized != builder.makeCompositeConstantView(separateStruct, xe_spv_words_view(signedMembers, 2), true));
    }
    builder.makeReturn(true);
    builder.leaveFunction();
    xe_spv_binary binary = {};
    CHECK(builder.dumpSized(binary));
    FILE* file = std::fopen(argv[1], "wb");
    CHECK(file);
    int written = std::fwrite(binary.words, sizeof(uint32_t), binary.count, file) == binary.count;
    if (std::fclose(file)) written = 0;
    xe_spv_binary_close(&binary);
    CHECK(written);
    std::puts("PASS: constant opcode/type/width/bit-pattern/order keys and distinct specialization IDs");
    return 0;
}
