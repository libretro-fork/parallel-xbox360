/* Typed shader interfaces only; packing, scenarios and goldens are C89. */
#include "forward_flow_bridge.cc"
extern "C" unsigned xe_loop_accuracy_native(const uint32_t* code,
    const uint32_t* loops, const float* constants, float* out) {
  xe::Memory memory;
  xe::gpu::RegisterFile registers;
  std::memcpy(&registers[xe::gpu::XE_GPU_REG_SHADER_CONSTANT_LOOP_00], loops, 128);
  std::memcpy(&registers[xe::gpu::XE_GPU_REG_SHADER_CONSTANT_000_X], constants, 96);
  registers[xe::gpu::XE_GPU_REG_SQ_VS_CONST] = 5u << 12;
  xe::gpu::ShaderInterpreter interpreter(registers, memory);
  std::memset(interpreter.temp_registers(), 0,
              xe::gpu::xenos::kMaxShaderTempRegisters * 16);
  ForwardSink sink;
  sink.out = out;
  interpreter.SetExportSink(&sink);
  interpreter.SetShader(xe::gpu::xenos::ShaderType::kVertex, code);
  interpreter.Execute();
  return sink.count;
}
