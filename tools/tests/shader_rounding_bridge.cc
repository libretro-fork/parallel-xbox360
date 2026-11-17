/* Existing typed translator/reference ABIs; all scenarios remain C89. */
#include "forward_flow_bridge.cc"
#include "alu_program_bridge.cc"
extern "C" size_t xe_rounding_spirv(const uint32_t* code,size_t count,
    uint32_t* out,size_t capacity) {
  using namespace xe::gpu;
  SpirvShader shader(xenos::ShaderType::kVertex,29,code,count,std::endian::native);
  xe::StringBuffer disasm;shader.AnalyzeUcode(disasm);
  SpirvShaderTranslator::Modification modification;
  modification.vertex.interpolator_mask=1;
  auto* translation=shader.GetOrCreateTranslation(modification.value);
  SpirvShaderTranslator::Features features(false);features.spirv_version=spv::Spv_1_3;
  features.signed_zero_inf_nan_preserve_float32=true;
  SpirvShaderTranslator translator(features,false,false,false,false,1,1);
  if(!translator.TranslateAnalyzedShader(*translation) || !translation->is_valid())return 0;
  auto binary=translation->translated_binary();
  if(binary.size()%4 || binary.size()/4>capacity)return 0;
  std::memcpy(out,binary.data(),binary.size());return binary.size()/4;
}
