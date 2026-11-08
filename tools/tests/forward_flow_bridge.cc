/* Typed shader ABI only; packed scenarios and checks are C89. */
#include "xenia/gpu/spirv_shader.h"
#include "xenia/gpu/spirv_shader_translator.h"
#include "xenia/gpu/shader_interpreter.h"
#include <cstring>
extern "C" size_t xe_forward_spirv(const uint32_t* code,size_t count,unsigned pixel,
    uint32_t* out,size_t capacity,unsigned* labels,uint32_t* bools) {
  using namespace xe::gpu;
  SpirvShader shader(pixel?xenos::ShaderType::kPixel:xenos::ShaderType::kVertex,29,code,count,std::endian::native);
  xe::StringBuffer disasm;shader.AnalyzeUcode(disasm);
  *labels=unsigned(shader.label_addresses().size());
  *bools=shader.constant_register_map().bool_bitmap[0];
  auto* translation=shader.GetOrCreateTranslation(0);
  SpirvShaderTranslator::Features features(true);features.spirv_version=spv::Spv_1_3;
  SpirvShaderTranslator translator(features,false,false,false,false,1,1);
  if(!translator.TranslateAnalyzedShader(*translation) || !translation->is_valid())return 0;
  auto binary=translation->translated_binary();
  if(binary.size()%4 || binary.size()/4>capacity)return 0;
  std::memcpy(out,binary.data(),binary.size());return binary.size()/4;
}
class ForwardSink final:public xe::gpu::ShaderInterpreter::ExportSink {
 public:
  float* out;unsigned count=0;
  void Export(xe::gpu::ucode::ExportRegister,const float* value,uint32_t mask) override {
    if(mask!=15){count=100;return;}
    std::memcpy(out,value,16);++count;
  }
};
extern "C" int xe_forward_interpret(const uint32_t* code,unsigned pixel,const float* inputs,float* out) {
  xe::Memory memory;xe::gpu::RegisterFile registers;
  xe::gpu::ShaderInterpreter interpreter(registers,memory);
  std::memcpy(interpreter.temp_registers(),inputs,32);
  ForwardSink sink;sink.out=out;interpreter.SetExportSink(&sink);
  interpreter.SetShader(pixel?xe::gpu::xenos::ShaderType::kPixel:xe::gpu::xenos::ShaderType::kVertex,code);
  interpreter.Execute();return sink.count==1;
}

class ForwardBuilder final:public xe::gpu::SpirvBuilder {
 public:
  using SpirvBuilder::SpirvBuilder;
  using spv::Builder::createSelectionMerge;
};
extern "C" int xe_forward_builder(unsigned phase,xe_spv_plan* plan,
    xe_spv_binary* out,uint32_t* ids) {
  ForwardBuilder builder(spv::Spv_1_3,0,nullptr);
  builder.addCapability(spv::Capability::Shader);
  builder.setMemoryModel(spv::AddressingModel::Logical,spv::MemoryModel::GLSL450);
  auto* main=builder.makeEntryPoint("main");
  builder.addEntryPoint(spv::ExecutionModel::GLCompute,main,"main");
  builder.addExecutionMode(main,spv::ExecutionMode::LocalSize,1,1,1);
  auto* entry=builder.getBuildPoint();
  auto* alive=&builder.makeNewBlock();auto* orphan=&builder.makeNewBlock();auto* merge=&builder.makeNewBlock();
  spv::Id type=builder.makeFloatType(32),one=builder.makeFloatConstant(1.0f),two=builder.makeFloatConstant(2.0f);
  spv::Id output=builder.createVariable(spv::NoPrecision,spv::StorageClass::Function,type);
  if(phase==1) {
    builder.createSelectionMerge(merge,spv::SelectionControlMask::MaskNone);
    builder.createConditionalBranch(builder.makeBoolConstant(true),alive,orphan);
  } else builder.createBranch(alive);
  builder.setBuildPoint(alive);builder.createBranch(merge);
  builder.setBuildPoint(orphan);
  spv::Id dead_value=builder.createNoContractionBinOp(spv::Op::OpFAdd,type,one,two);
  builder.addName(dead_value,"dead-value");builder.addName(orphan->getId(),"dead-block");
  builder.createBranch(merge);builder.setBuildPoint(merge);
  spv::Id phi=builder.createQuadOp(spv::Op::OpPhi,type,one,alive->getId(),dead_value,orphan->getId());
  builder.createStore(phi,output);builder.makeReturn(true);builder.leaveFunction();
  ids[0]=orphan->getId();ids[1]=dead_value;ids[2]=phi;
  if(!builder.dumpSized(*out,plan))return 0;
  xe_spv_binary legacy={};
  bool same=builder.dump(legacy) && legacy.count==out->count &&
      !std::memcmp(legacy.words,out->words,out->count*4);
  xe_spv_binary_close(&legacy);
  (void)entry;
  return same?1:0;
}
