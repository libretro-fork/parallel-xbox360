/* Existing typed interpreter lifetime only; measured loop stays C89. */
#include "xenia/gpu/shader_interpreter.h"
#include <cstring>
class SelectionBenchSink final : public xe::gpu::ShaderInterpreter::ExportSink {
 public:
  uint32_t words[4]={};
  unsigned count=0;
  void Export(xe::gpu::ucode::ExportRegister target,const float* value,uint32_t mask) override {
    if(static_cast<unsigned>(target)!=62 || mask!=15){count=0x80000000u;return;}
    std::memcpy(words,value,16);++count;
  }
};
struct SelectionBench {
  xe::Memory memory;
  xe::gpu::RegisterFile registers;
  xe::gpu::ShaderInterpreter interpreter;
  SelectionBenchSink sink;
  SelectionBench():interpreter(registers,memory){interpreter.SetExportSink(&sink);}
};
extern "C" void* xe_selection_cpu_create(){return new SelectionBench;}
extern "C" void xe_selection_cpu_setup(void* opaque,const uint32_t* code,const uint32_t* values){
  auto* b=static_cast<SelectionBench*>(opaque);
  std::memset(b->interpreter.temp_registers(),0,64*16);
  std::memcpy(b->interpreter.temp_registers(),values,48);
  std::memcpy(&b->registers[xe::gpu::XE_GPU_REG_SHADER_CONSTANT_000_X],values,48);
  b->registers[xe::gpu::XE_GPU_REG_SQ_VS_CONST]=2u<<12;
  b->sink.count=0;b->interpreter.SetShader(xe::gpu::xenos::ShaderType::kVertex,code);
}
extern "C" void xe_selection_cpu_step(void* opaque){static_cast<SelectionBench*>(opaque)->interpreter.Execute();}
extern "C" const uint32_t* xe_selection_cpu_result(void* opaque,unsigned* count){
  auto* b=static_cast<SelectionBench*>(opaque);*count=b->sink.count;return b->sink.words;
}
extern "C" void xe_selection_cpu_destroy(void* opaque){delete static_cast<SelectionBench*>(opaque);}
