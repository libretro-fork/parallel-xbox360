/* Actual guest translator, with C89-shaped raw microcode construction. */
#include "xenia/gpu/spirv_shader_translator.h"
#include "xenia/base/string_buffer.h"
#include <bit>
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"memexport line %u: %s\n",unsigned(__LINE__),#x); std::abort(); } } while(0)
#include "core_spv_memexport_program.h"
int main() {
    const char* path=std::getenv("SPV_GUEST_OUTPUT"); CHECK(path);
    FILE* file=std::fopen(path,"wb"); CHECK(file);
    const char* validation=std::getenv("SPV_MEMEXPORT_VALIDATE_DIR");
    const bool skip=std::getenv("SPV_MEMEXPORT_SKIP_SCALE_HOST")!=nullptr;
    uint32_t words[21]; size_t translations=0, stores=0;
    for(unsigned profile=0;profile<4;++profile) {
        xe::gpu::SpirvShaderTranslator translator(xe::gpu::SpirvShaderTranslator::Features(true),
            false,false,(profile&1)!=0,false,profile>=2?2:1,profile>=2?3:1);
        for(unsigned stage=0;stage<2;++stage) for(unsigned mask=0;mask<32;++mask) {
            if(skip && profile==2 && stage && mask) continue;
            const size_t count=xe_spv_memexport_test_program(words,mask);
            xe::gpu::Shader shader(stage?xe::gpu::xenos::ShaderType::kPixel:xe::gpu::xenos::ShaderType::kVertex,
                1+mask+stage*32,words,count,std::endian::native);
            xe::StringBuffer disassembly; shader.AnalyzeUcode(disassembly);
            CHECK(shader.memexport_eM_written()==mask);
            if(mask) CHECK(shader.memexport_stream_constants().count(1));
            uint64_t modification=stage?translator.GetDefaultPixelShaderModification(8):translator.GetDefaultVertexShaderModification(8);
            auto* translation=shader.GetOrCreateTranslation(modification);
            CHECK(translator.TranslateAnalyzedShader(*translation) && translation->is_valid());
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
            CHECK(std::fwrite(&bytes,4,1,file)==1 && std::fwrite(binary.data(),1,bytes,file)==bytes);
            if(validation) {
                char name[2048]; CHECK(std::snprintf(name,sizeof(name),"%s/p%u-s%u-m%u.spv",validation,profile,stage,mask)>0);
                FILE* module=std::fopen(name,"wb"); CHECK(module);
                CHECK(std::fwrite(binary.data(),1,bytes,module)==bytes && !std::fclose(module));
            }
            ++translations; if(mask) ++stores;
        }
    }
    CHECK(!std::fclose(file));
    std::printf("PASS: %zu actual guest translations, %zu memory-export variants, mask/stage/profile matrix\n",translations,stores);
}
