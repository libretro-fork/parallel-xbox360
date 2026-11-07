/* Typed interface only; scenarios and raw instruction packing live in C89. */
#include "xenia/gpu/shader_interpreter.h"
#include <cstring>
class LoopSink final : public xe::gpu::ShaderInterpreter::ExportSink {
 public:
  float* values; unsigned capacity, count;
  void Export(xe::gpu::ucode::ExportRegister, const float* value, uint32_t mask) override {
    if (!(mask & 1) || count >= capacity) { count = capacity + 1; return; }
    values[count++] = value[0];
  }
};
extern "C" unsigned xe_loop_native(const uint32_t* code, const uint32_t* constants,
                                  float* output, unsigned capacity, unsigned repeats) {
  xe::Memory memory;
  xe::gpu::RegisterFile registers;
  std::memcpy(&registers[xe::gpu::XE_GPU_REG_SHADER_CONSTANT_LOOP_00],constants,32*sizeof(uint32_t));
  xe::gpu::ShaderInterpreter interpreter(registers,memory);
  for (unsigned i=0;i<64;++i) for(unsigned j=0;j<4;++j)
    interpreter.temp_registers()[i*4+j]=float(i)+0.25f;
  LoopSink sink; sink.values=output; sink.capacity=capacity; sink.count=0;
  interpreter.SetExportSink(&sink);
  interpreter.SetShader(xe::gpu::xenos::ShaderType::kVertex,code);
  while(repeats--) { sink.count=0; interpreter.Execute(); }
  return sink.count;
}
