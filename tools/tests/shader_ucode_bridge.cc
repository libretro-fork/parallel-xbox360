/* Actual Shader lifetime/view ABI only. Algorithms and corpus are C89. */
#include "xenia/gpu/shader.h"
extern "C" void* xe_ucode_open(const uint32_t* words,size_t count,unsigned swap) {
  std::endian endian=std::endian::native;
  if(swap)endian=endian==std::endian::little?std::endian::big:std::endian::little;
  return new xe::gpu::Shader(xe::gpu::xenos::ShaderType::kVertex,0,words,count,endian);
}
extern "C" void xe_ucode_close(void* owner) { delete static_cast<xe::gpu::Shader*>(owner); }
extern "C" const uint32_t* xe_ucode_view(void* owner,size_t* count) {
  const auto* shader=static_cast<const xe::gpu::Shader*>(owner);
  *count=shader->ucode_data().size();return shader->ucode_data().data();
}
extern "C" size_t xe_ucode_shader_size(void) { return sizeof(xe::gpu::Shader); }
extern "C" unsigned xe_ucode_required_alignment(void) {
#ifdef XE_SHADER_UCODE_LARGE_ALIGNMENT
  return XE_SHADER_UCODE_LARGE_ALIGNMENT;
#else
  return 0;
#endif
}
