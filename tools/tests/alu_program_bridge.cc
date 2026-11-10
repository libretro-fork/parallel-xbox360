/* Typed CPU reference setup/export ABI only. The corpus and checks are C89. */
#include "xenia/gpu/shader_interpreter.h"
#include <cstring>
extern "C" void xe_alu_program_export(uint32_t*,unsigned,const void*,uint32_t);
class AluProgramSink final : public xe::gpu::ShaderInterpreter::ExportSink {
 public:
  uint32_t *out;
  void Export(xe::gpu::ucode::ExportRegister target,const float *value,uint32_t lanes) override {
    xe_alu_program_export(out,static_cast<unsigned>(target),value,lanes);
  }
};
extern "C" unsigned xe_alu_program_reference(const uint32_t *code,
    const uint32_t *temporary,unsigned temporary_count,const uint32_t *constants,
    unsigned constant_count,unsigned window_base,unsigned window_size,uint32_t *output) {
  xe::Memory memory;xe::gpu::RegisterFile registers;
  xe::gpu::ShaderInterpreter interpreter(registers,memory);
  std::memset(interpreter.temp_registers(),0,64*16);
  std::memcpy(interpreter.temp_registers(),temporary,temporary_count*16);
  auto *bank=&registers[xe::gpu::XE_GPU_REG_SHADER_CONSTANT_000_X];
  std::memset(bank,0,512*16);std::memcpy(bank,constants,constant_count*16);
  registers[xe::gpu::XE_GPU_REG_SQ_VS_CONST]=window_base|(window_size<<12);
  std::memset(output,0,7*4);AluProgramSink sink;sink.out=output;
  interpreter.SetExportSink(&sink);interpreter.SetShader(xe::gpu::xenos::ShaderType::kVertex,code);
  interpreter.Execute();return output[6]==15?1:4;
}
