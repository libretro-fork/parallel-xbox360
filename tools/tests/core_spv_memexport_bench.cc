/* Actual guest translator, with C89-shaped raw microcode construction. */
#include "xenia/gpu/spirv_shader_translator.h"
#include "xenia/base/string_buffer.h"
#include "core_pipeline_bench_profile.h"
#include <new>
#include <bit>
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"memexport line %u: %s\n",unsigned(__LINE__),#x); std::abort(); } } while(0)
#include "core_spv_memexport_program.h"
void* operator new(size_t bytes) {
    if (void* value = xe_bench_malloc(bytes ? bytes : 1)) return value;
    throw std::bad_alloc();
}
void* operator new[](size_t bytes) { return ::operator new(bytes); }
void operator delete(void* value) noexcept { xe_bench_free(value); }
void operator delete[](void* value) noexcept { xe_bench_free(value); }
void operator delete(void* value, size_t) noexcept { xe_bench_free(value); }
void operator delete[](void* value, size_t) noexcept { xe_bench_free(value); }
int main() {
    const char* path=std::getenv("SPV_GUEST_OUTPUT"); CHECK(path);
    FILE* file=std::fopen(path,"wb"); CHECK(file);
    const unsigned rounds=5;
    size_t measured=0,total_calls=0,total_requested=0; uint64_t total_us=0;
    const bool skip=std::getenv("SPV_MEMEXPORT_SKIP_SCALE_HOST")!=nullptr;
    uint32_t words[21]; size_t translations=0, stores=0;
    for(unsigned profile=0;profile<4;++profile) {
        xe::gpu::SpirvShaderTranslator translator(xe::gpu::SpirvShaderTranslator::Features(true),
            false,false,(profile&1)!=0,false,profile>=2?2:1,profile>=2?3:1);
        for(unsigned round=0;round<rounds;++round) for(unsigned stage=0;stage<2;++stage) for(unsigned mask=0;mask<32;++mask) {
            if(skip && profile==2 && stage && mask) continue;
            const size_t count=xe_spv_memexport_test_program(words,mask);
            xe::gpu::Shader shader(stage?xe::gpu::xenos::ShaderType::kPixel:xe::gpu::xenos::ShaderType::kVertex,
                1+mask+stage*32,words,count,std::endian::native);
            xe::StringBuffer disassembly; shader.AnalyzeUcode(disassembly);
            CHECK(shader.memexport_eM_written()==mask);
            if(mask) CHECK(shader.memexport_stream_constants().count(1));
            uint64_t modification=stage?translator.GetDefaultPixelShaderModification(8):translator.GetDefaultVertexShaderModification(8);
            auto* translation=shader.GetOrCreateTranslation(modification);
            size_t calls=xe_bench_allocations(), requested=xe_bench_requested();
            uint64_t start=xe_bench_time();
            CHECK(translator.TranslateAnalyzedShader(*translation) && translation->is_valid());
            uint64_t us=xe_bench_time()-start;
            if(round) { ++measured; total_calls+=xe_bench_allocations()-calls; total_requested+=xe_bench_requested()-requested; total_us+=us; }
            const auto& binary=translation->translated_binary();
            uint32_t bytes=uint32_t(binary.size()); CHECK(bytes);
            if(profile==2 && stage && mask) {
                const uint32_t* code=(const uint32_t*)binary.data(); bool coordinates=false;
                for(size_t offset=5;offset<binary.size()/4;) {
                    uint32_t length=code[offset]>>16; CHECK(length && length<=binary.size()/4-offset);
                    if((code[offset]&65535)==71 && length==4 && code[offset+2]==11 && code[offset+3]==15) coordinates=true;
                    offset+=length;
                }
                CHECK(coordinates);
            }
            if(!round) CHECK(std::fwrite(&bytes,4,1,file)==1 && std::fwrite(binary.data(),1,bytes,file)==bytes);
            ++translations; if(mask) ++stores;
        }
    }
    CHECK(!std::fclose(file));
    std::printf("PROFILE translations=%zu cpp_calls=%zu cpp_requested=%zu ms=%.3f\n",measured,total_calls,total_requested,double(total_us)/1000.0);
    std::printf("PASS: %zu actual guest translations, %zu memory-export variants, mask/stage/profile matrix\n",translations,stores);
}
