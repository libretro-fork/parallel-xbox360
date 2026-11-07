/* Typed shader/interpreter ABI only. Packed inputs and checks are C89. */
#include "xenia/gpu/spirv_shader.h"
#include "xenia/gpu/spirv_shader_translator.h"
#include "xenia/gpu/shader_interpreter.h"
#include <cstring>
extern "C" size_t xe_rtz_native_spirv(const uint32_t* code,size_t count,
    unsigned pixel,unsigned hardware,unsigned ftz,uint32_t* out,size_t capacity) {
  using namespace xe::gpu;
  SpirvShader shader(pixel?xenos::ShaderType::kPixel:xenos::ShaderType::kVertex,
      19,code,count,std::endian::native);
  xe::StringBuffer disasm;shader.AnalyzeUcode(disasm);
  auto* translation=shader.GetOrCreateTranslation(0);
  SpirvShaderTranslator::Features features(true);
  features.spirv_version=spv::Spv_1_3;
  features.rounding_mode_rtz_float32=(hardware&1)!=0;
  features.allow_float_contraction=(hardware&2)!=0;
  features.denorm_flush_to_zero_float32=ftz!=0;
  if(hardware&4) {
    features.spirv_version=spv::Spv_1_0;
    features.signed_zero_inf_nan_preserve_float32=false;
    features.rounding_mode_rte_float32=false;
    features.rounding_mode_rtz_float32=false;
    features.denorm_flush_to_zero_float32=false;
  }
  SpirvShaderTranslator translator(features,false,false,false,false,1,1);
  if(!translator.TranslateAnalyzedShader(*translation) || !translation->is_valid())return 0;
  auto binary=translation->translated_binary();
  if(binary.size()%4 || binary.size()/4>capacity)return 0;
  std::memcpy(out,binary.data(),binary.size());return binary.size()/4;
}
class RoundingSink final:public xe::gpu::ShaderInterpreter::ExportSink {
 public:
  uint32_t* out;unsigned count=0;
  void Export(xe::gpu::ucode::ExportRegister,const float* value,uint32_t mask) override {
    if(mask!=15){count=100;return;}
    std::memcpy(out,value,16);++count;
  }
};
extern "C" int xe_rtz_native_interpret(const uint32_t* code,const uint32_t* inputs,
    unsigned pixel,uint32_t* out) {
  xe::Memory memory;xe::gpu::RegisterFile registers;
  xe::gpu::ShaderInterpreter interpreter(registers,memory);
  std::memcpy(interpreter.temp_registers(),inputs,32);
  RoundingSink sink;sink.out=out;interpreter.SetExportSink(&sink);
  interpreter.SetShader(pixel?xe::gpu::xenos::ShaderType::kPixel:xe::gpu::xenos::ShaderType::kVertex,code);
  interpreter.Execute();return sink.count==1;
}
