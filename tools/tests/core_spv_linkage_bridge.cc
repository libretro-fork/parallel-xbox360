/* Typed builder interop; counted-name wire checks use C89-shaped code. */
#include "SpvBuilder.h"
#include <cstdio>
#include <cstring>

static int check_linkage(const uint32_t* words, size_t count,
    uint32_t id, const char* expected, size_t bytes, uint32_t linkage)
{
    size_t offset = 5;
    size_t seen = 0;
    size_t string_words = bytes / 4 + 1;
    size_t i;
    while (offset < count) {
        size_t length = words[offset] >> 16;
        uint32_t op = words[offset] & 65535;
        if (!length || length > count - offset) return 0;
        if (op == (uint32_t)spv::Op::OpDecorate && length >= 4 &&
            words[offset + 1] == id &&
            words[offset + 2] == (uint32_t)spv::Decoration::LinkageAttributes) {
            const unsigned char* text = (const unsigned char*)(words + offset + 3);
            if (length != 4 + string_words ||
                words[offset + length - 1] != linkage ||
                (bytes && std::memcmp(text, expected, bytes))) return 0;
            for (i = bytes; i < string_words * 4; ++i)
                if (text[i]) return 0;
            ++seen;
        }
        offset += length;
    }
    return seen == 1;
}

int main(int argc, char** argv)
{
    spv::Builder builder(spv::Spv_1_3, 0, nullptr);
    const size_t lengths[] = {0, 1, 3, 4, 7, 8, 15, 16, 31, 32, 63, 64, 255, 1024};
    const spv::LinkageType modes[] = {spv::LinkageType::Export,
        spv::LinkageType::Import, spv::LinkageType::LinkOnceODR};
    uint32_t ids[3][14];
    char input[1040], expected[1025];
    xe_spv_binary binary = {};
    size_t mode, test, i;
    if (argc != 2) return 1;
    builder.setMemoryModel(spv::AddressingModel::Logical, spv::MemoryModel::GLSL450);
    for (mode = 0; mode < 3; ++mode) {
        for (test = 0; test < 14; ++test) {
            spv::Block* entry;
            size_t bytes = lengths[test];
            std::memset(input, 'a' + (int)test, bytes);
            std::memcpy(input + bytes, "(u32,f32)", 10);
            spv::Function* function = builder.makeFunctionEntry(spv::NoPrecision,
                builder.makeVoidType(), input, modes[mode], {}, {}, &entry);
            ids[mode][test] = function->getId();
            std::memset(input, 'x', sizeof(input));
            // Force later name-payload growth before consuming the borrowed prefix.
            for (i = 0; i < 32; ++i) builder.addName(function->getId(), "later_name_payload_which_must_not_relocate_previous_names");
            builder.enterFunction(function);
            builder.makeReturn(true);
            builder.leaveFunction();
        }
    }
    // A missing OpName retains the old empty linkage spelling.
    spv::Block* entry;
    spv::Function* unnamed = builder.makeFunctionEntry(spv::NoPrecision,
        builder.makeVoidType(), nullptr, spv::LinkageType::Export, {}, {}, &entry);
    builder.enterFunction(unnamed);
    builder.makeReturn(true);
    builder.leaveFunction();
    if (!builder.dumpSized(binary)) return 2;
    for (mode = 0; mode < 3; ++mode) {
        for (test = 0; test < 14; ++test) {
            std::memset(expected, 'a' + (int)test, lengths[test]);
            if (!check_linkage(binary.words, binary.count, ids[mode][test],
                    expected, lengths[test], (uint32_t)modes[mode])) return 3;
        }
    }
    if (!check_linkage(binary.words, binary.count, unnamed->getId(), "", 0,
            (uint32_t)spv::LinkageType::Export)) return 4;
    FILE* output = std::fopen(argv[1], "wb");
    if (!output) return 5;
    int ok = std::fwrite(binary.words, sizeof(uint32_t), binary.count, output) == binary.count;
    if (std::fclose(output)) ok = 0;
    xe_spv_binary_close(&binary);
    if (!ok) return 6;
    std::puts("PASS: 43 import/export/ODR names, counted prefixes/padding, overwritten caller inputs and stable payload views");
    return 0;
}
