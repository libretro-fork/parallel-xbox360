/* Typed interpreter ABI only; instruction packing and assertions are C89. */
#include "xenia/gpu/shader_interpreter.h"
#include <cstring>
class VertexAddressSink final : public xe::gpu::ShaderInterpreter::ExportSink {
 public:
  uint32_t* output;
  unsigned count=0;
  void Export(xe::gpu::ucode::ExportRegister target,const float* value,uint32_t mask) override {
    if(target!=xe::gpu::ucode::ExportRegister::kVSPosition || mask!=15) { count=100;return; }
    std::memcpy(output,value,16);++count;
  }
};
extern "C" unsigned xe_vertex_address_native(const uint32_t* code,uint32_t index,uint32_t* output) {
  xe::Memory memory;
  xe::gpu::RegisterFile registers;
  xe::gpu::ShaderInterpreter interpreter(registers,memory);
  std::memset(interpreter.temp_registers(),0,64*16);
  std::memcpy(interpreter.temp_registers(),&index,4);
  VertexAddressSink sink;sink.output=output;
  interpreter.SetExportSink(&sink);
  interpreter.SetShader(xe::gpu::xenos::ShaderType::kVertex,code);
  interpreter.Execute();return sink.count;
}
