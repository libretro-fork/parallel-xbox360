/* Typed CPU interpreter fixture only. Packed inputs and goldens are C89. */
#include "xenia/gpu/shader_interpreter.h"
#include <cstring>
class AluOperandSink final : public xe::gpu::ShaderInterpreter::ExportSink {
 public:
  uint32_t* output;unsigned count=0;
  void Export(xe::gpu::ucode::ExportRegister,const float* value,uint32_t mask) override {
    if(mask!=15){count=100;return;}
    std::memcpy(output,value,16);++count;
  }
};
struct AluOperandContext {
  xe::Memory memory;
  xe::gpu::RegisterFile registers;
  xe::gpu::ShaderInterpreter interpreter;
  AluOperandSink sink;
  unsigned pixel;
  AluOperandContext(const uint32_t* temporary,const uint32_t* constants,unsigned stage)
      : interpreter(registers,memory),pixel(stage) {
    registers[xe::gpu::XE_GPU_REG_SQ_VS_CONST]=511u<<12;
    registers[xe::gpu::XE_GPU_REG_SQ_PS_CONST]=511u<<12;
    std::memcpy(&registers[xe::gpu::XE_GPU_REG_SHADER_CONSTANT_000_X],constants,48);
    std::memcpy(interpreter.temp_registers(),temporary,32);
    interpreter.SetExportSink(&sink);
  }
};
extern "C" void* xe_alu_operand_context_new(const uint32_t* temporary,
    const uint32_t* constants,unsigned pixel) {
  return new AluOperandContext(temporary,constants,pixel);
}
extern "C" unsigned xe_alu_operand_context_run(void* opaque,const uint32_t* code,uint32_t* output) {
  auto& context=*static_cast<AluOperandContext*>(opaque);
  context.sink.output=output;context.sink.count=0;
  context.interpreter.SetShader(context.pixel?xe::gpu::xenos::ShaderType::kPixel:xe::gpu::xenos::ShaderType::kVertex,code);
  context.interpreter.Execute();return context.sink.count;
}
extern "C" void xe_alu_operand_context_free(void* opaque) {
  delete static_cast<AluOperandContext*>(opaque);
}
extern "C" int xe_alu_operand_reference(const uint32_t* code,const uint32_t* temporary,
    const uint32_t* constants,unsigned pixel,uint32_t* output) {
  AluOperandContext context(temporary,constants,pixel);
  return xe_alu_operand_context_run(&context,code,output)==1;
}
