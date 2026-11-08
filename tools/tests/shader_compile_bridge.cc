/* Typed lifetime boundary only. Corpus, timing and checking are C89. */
#include "xenia/gpu/spirv_shader.h"
#include "xenia/gpu/spirv_shader_translator.h"
using namespace xe::gpu;
struct ShaderCompileContext {
  static SpirvShaderTranslator::Features Features(unsigned profile) {
    SpirvShaderTranslator::Features f(false);
    f.spirv_version=spv::Spv_1_3;
    f.image_view_format_swizzle=(profile&1)!=0;
    return f;
  }
  SpirvShaderTranslator translator;
  std::unique_ptr<SpirvShader> shader;
  xe::StringBuffer disassembly;
  explicit ShaderCompileContext(unsigned profile)
      :translator(Features(profile),false,false,false,false,
                  profile&2?4:1,profile&2?4:1) {}
};
extern "C" void* xe_compile_open(unsigned profile) {
  return new ShaderCompileContext(profile);
}
extern "C" void xe_compile_close(void* p) {
  delete static_cast<ShaderCompileContext*>(p);
}
extern "C" int xe_compile_step(void* p,const uint32_t* code,size_t count,
                                unsigned pixel,const void** data,size_t* size) {
  auto& c=*static_cast<ShaderCompileContext*>(p);
  c.shader=std::make_unique<SpirvShader>(pixel?xenos::ShaderType::kPixel:
      xenos::ShaderType::kVertex,0,code,count,std::endian::native);
  c.disassembly.Reset();
  c.shader->AnalyzeUcode(c.disassembly);
  auto* translation=c.shader->GetOrCreateTranslation(0);
  if(!c.translator.TranslateAnalyzedShader(*translation) || !translation->is_valid())return 0;
  auto binary=translation->translated_binary();
  *data=binary.data();*size=binary.size();
  return !binary.empty();
}
