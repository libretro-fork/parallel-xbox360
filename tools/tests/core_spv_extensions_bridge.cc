/* Public producer oracle: sorted snapshots include disable/re-add operations. */
#include "SpvBuilder.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); return 1; } } while(0)
static bool snapshot(spv::Builder& builder, FILE* file, const char* last) {
    xe_spv_binary owned = {};
    std::vector<unsigned> reference;
    builder.dump(reference);
    if (!builder.dumpSized(owned) || reference.size() != owned.count ||
        std::memcmp(reference.data(),owned.words,owned.count*sizeof(uint32_t))) return false;
    bool ok = std::fwrite(owned.words,sizeof(uint32_t),owned.count,file)==owned.count;
    if (last) {
        FILE* output = std::fopen(last,"wb");
        if (!output) ok = false;
        else {
            if(std::fwrite(owned.words,sizeof(uint32_t),owned.count,output)!=owned.count) ok = false;
            if(std::fclose(output)) ok = false;
        }
    }
    xe_spv_binary_close(&owned);
    return ok;
}
int main(int argc, char** argv) {
    if(argc!=3) return 1;
    const bool arbitrary = std::atoi(argv[2]) != 0;
    spv::Builder builder(spv::Spv_1_3,0,nullptr);
    builder.addCapability(spv::Capability::Shader);
    builder.setMemoryModel(spv::AddressingModel::Logical,spv::MemoryModel::GLSL450);
    auto* main = builder.makeEntryPoint("main");
    builder.addEntryPoint(spv::ExecutionModel::GLCompute,main,"main");
    builder.addExecutionMode(main,spv::ExecutionMode::LocalSize,1,1,1);
    builder.makeReturn(true); builder.leaveFunction();
    FILE* output = std::fopen(argv[1],"wb"); CHECK(output);
    CHECK(snapshot(builder,output,nullptr));
    builder.addExtension("SPV_KHR_vulkan_memory_model");
    builder.addExtension("SPV_KHR_16bit_storage");
    builder.addExtension("SPV_KHR_storage_buffer_storage_class");
    builder.addExtension("SPV_KHR_16bit_storage");
    CHECK(snapshot(builder,output,nullptr));
    builder.removeExtension("SPV_KHR_16bit_storage");
    builder.removeExtension("missing");
    CHECK(snapshot(builder,output,nullptr));
    for(unsigned i=0;i<10000;++i) {
        builder.addExtension("SPV_KHR_16bit_storage");
        builder.removeExtension("SPV_KHR_16bit_storage");
    }
    CHECK(snapshot(builder,output,nullptr));
    builder.addExtension("SPV_KHR_16bit_storage");
    builder.promoteIncorporatedExtension("SPV_KHR_16bit_storage",
        "SPV_KHR_vulkan_memory_model",spv::Spv_1_5);
    CHECK(snapshot(builder,output,nullptr));
    builder.promoteIncorporatedExtension("SPV_KHR_vulkan_memory_model",
        "SPV_KHR_16bit_storage",spv::Spv_1_3);
    CHECK(snapshot(builder,output,nullptr));
    if(arbitrary) {
        char name[128];
        for(unsigned i=129;i;--i) {
            std::snprintf(name,sizeof(name),"SPV_FAKE_name_%03u",i-1);
            builder.addExtension(name);
            std::memset(name,'X',sizeof(name));
        }
        builder.addExtension(""); builder.addExtension("SPV_UTF8_\x7f");
        builder.addExtension("SPV_UTF8_\x80"); builder.addExtension("SPV_UTF8_\xff");
        CHECK(snapshot(builder,output,nullptr));
        for(unsigned i=0;i<129;i+=2) {
            std::snprintf(name,sizeof(name),"SPV_FAKE_name_%03u",i);
            builder.removeExtension(name);
        }
        CHECK(snapshot(builder,output,nullptr));
        std::memset(name,'Z',sizeof(name)-1); name[sizeof(name)-1]=0;
        builder.addExtension(name); std::memset(name,'X',sizeof(name));
    }
    char last[2048];
    CHECK(std::snprintf(last,sizeof(last),"%s.last.spv",argv[1])>0);
    CHECK(snapshot(builder,output,last));
    CHECK(!std::fclose(output));
    std::puts("PASS: extension sorting, owned input, growth, duplicates, disable/re-add and incorporated promotion snapshots");
    return 0;
}
