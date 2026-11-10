/* Typed production translator access; orchestration/fixtures are C89. */
#include "xenia/gpu/spirv_shader.h"
#include "xenia/gpu/spirv_shader_translator.h"
#include <cstring>
#include "xenia/gpu/dxil/guest_texture_stage.h"
using namespace xe::gpu;
struct BindingTranslationFixture {
  SpirvShader shader;
  Shader::Translation* variants[8];
  BindingTranslationFixture(const uint32_t* code,size_t count,unsigned pixel)
      : shader(pixel?xenos::ShaderType::kPixel:xenos::ShaderType::kVertex,
               0,code,count,std::endian::native) {}
};
extern "C" void* xe_binding_translate_new(const uint32_t* code,size_t count,unsigned pixel) {
  auto* fixture=new BindingTranslationFixture(code,count,pixel);
  xe::StringBuffer scratch;fixture->shader.AnalyzeUcode(scratch);
  if(!fixture->shader.is_ucode_analyzed()){delete fixture;return nullptr;}
  for(unsigned i=0;i<8;++i)fixture->variants[i]=fixture->shader.GetOrCreateTranslation(i);
  return fixture;
}
extern "C" void xe_binding_translate_free(void* owner) {delete static_cast<BindingTranslationFixture*>(owner);}
extern "C" void xe_binding_translate_lock(void* owner,unsigned lock) {
  auto& mutex=static_cast<BindingTranslationFixture*>(owner)->shader.native_emission_mutex();
  if(lock)mutex.lock();else mutex.unlock();
}
extern "C" int xe_binding_translate_step(void* owner,unsigned index) {
  auto& fixture=*static_cast<BindingTranslationFixture*>(owner);
  SpirvShaderTranslator::Features features(false);features.spirv_version=spv::Spv_1_3;
  SpirvShaderTranslator translator(features,false,false,false,false,1,1);
  if(index>=8 || !translator.TranslateAnalyzedShader(*fixture.variants[index]))return -1;
  SpirvShader reference(fixture.shader.type(),0,fixture.shader.ucode_dwords(),
                        fixture.shader.ucode_dword_count(),std::endian::native);
  xe::StringBuffer scratch;reference.AnalyzeUcode(scratch);
  auto* expected=reference.GetOrCreateTranslation(index);
  if(!translator.TranslateAnalyzedShader(*expected))return -2;
  auto actual=fixture.variants[index]->translated_binary();auto bytes=expected->translated_binary();
  if(!fixture.shader.bindings_ready())return -4;
  auto textures=fixture.shader.GetTextureBindingsAfterTranslation();
  auto ref_textures=reference.GetTextureBindingsAfterTranslation();
  auto samplers=fixture.shader.GetSamplerBindingsAfterTranslation();
  auto ref_samplers=reference.GetSamplerBindingsAfterTranslation();
  if(textures.size()!=ref_textures.size() || samplers.size()!=ref_samplers.size() ||
     fixture.shader.GetUsedTextureMaskAfterTranslation()!=reference.GetUsedTextureMaskAfterTranslation() ||
     (textures.size() && std::memcmp(textures.data(),ref_textures.data(),textures.size_bytes())) ||
     (samplers.size() && std::memcmp(samplers.data(),ref_samplers.data(),samplers.size_bytes())))return -5;
  if(!actual.empty() && actual.size()==bytes.size() && !std::memcmp(actual.data(),bytes.data(),actual.size()))return 1;
  // Existing preloading assigns IDs before main; first discovery assigns them
  // during instruction emission. Match the exact independent warm form too.
  SpirvShader warm(fixture.shader.type(),0,fixture.shader.ucode_dwords(),
                   fixture.shader.ucode_dword_count(),std::endian::native);
  warm.AnalyzeUcode(scratch);
  if(!translator.TranslateAnalyzedShader(*warm.GetOrCreateTranslation(8)))return -6;
  auto* warm_expected=warm.GetOrCreateTranslation(index);
  if(!translator.TranslateAnalyzedShader(*warm_expected))return -7;
  bytes=warm_expected->translated_binary();
  return !actual.empty() && actual.size()==bytes.size() && !std::memcmp(actual.data(),bytes.data(),actual.size())?1:-3;
}

// Deterministic competing publication after module emission, before PostTranslation.
class LateBindingTranslator final : public SpirvShaderTranslator {
 public:
  LateBindingTranslator(const Features& features,SpirvShader& shader,const XeDxilTextureLayout& layout)
      : SpirvShaderTranslator(features,false,false,false,false,1,1),shader_(shader),layout_(layout) {}
  bool published=false;
 protected:
  void PostTranslation() override {
    published=shader_.PublishNativeBindings(layout_);
    SpirvShaderTranslator::PostTranslation();
  }
 private:
  SpirvShader& shader_;
  const XeDxilTextureLayout& layout_;
};
extern "C" int xe_binding_translate_late(const uint32_t* code,size_t count,unsigned pixel,const XeDxilTextureLayout* layout) {
  SpirvShader shader(pixel?xenos::ShaderType::kPixel:xenos::ShaderType::kVertex,0,code,count,std::endian::native);
  xe::StringBuffer scratch;shader.AnalyzeUcode(scratch);
  SpirvShaderTranslator::Features features(false);features.spirv_version=spv::Spv_1_3;
  LateBindingTranslator translator(features,shader,*layout);
  const bool valid=translator.TranslateAnalyzedShader(*shader.GetOrCreateTranslation(0));
  if(!translator.published || !shader.bindings_ready() ||
     shader.GetTextureBindingsAfterTranslation().size()!=layout->texture_count ||
     shader.GetSamplerBindingsAfterTranslation().size()!=layout->sampler_count ||
     shader.GetUsedTextureMaskAfterTranslation()!=layout->used_texture_mask)return -1;
  return valid?1:0;
}
