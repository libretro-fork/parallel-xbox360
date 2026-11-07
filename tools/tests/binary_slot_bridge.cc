/* Typed ABI/destructor adapter only; scenarios live in the C89 driver. */
#include "xenia/gpu/d3d12/d3d12_shader.h"
#include <cstdlib>
#include <cstring>
using xe::gpu::Shader;
using xe::gpu::d3d12::D3D12Shader;
extern "C" void* xe_binary_native_shader(unsigned pixel) {
  const uint32_t code[3]={0,0,0};
  return new D3D12Shader(pixel?xe::gpu::xenos::ShaderType::kPixel:xe::gpu::xenos::ShaderType::kVertex,
      19,code,3,std::endian::native);
}
extern "C" void* xe_binary_native_translation(void* shader,uint64_t modification) {
  return static_cast<Shader*>(shader)->GetOrCreateTranslation(modification);
}
extern "C" xe_binary_snapshot xe_binary_native_read(const void* translation) {
  return static_cast<const D3D12Shader::D3D12Translation*>(translation)->native_snapshot();
}
extern "C" int xe_binary_native_publish(void* translation,void* binary) {
  return static_cast<D3D12Shader::D3D12Translation*>(translation)->PublishNativeBinary(
      static_cast<XeDxilShaderBinary*>(binary));
}
extern "C" void xe_binary_native_remove(void* shader,uint64_t modification) {
  static_cast<Shader*>(shader)->DestroyTranslation(modification);
}
extern "C" void xe_binary_native_close(void* shader) {delete static_cast<Shader*>(shader);}
extern "C" void* xe_binary_native_create(unsigned tag) {
  auto* binary=static_cast<XeDxilShaderBinary*>(std::calloc(1,sizeof(XeDxilShaderBinary)));
  if(!binary)return nullptr;
  binary->data=static_cast<unsigned char*>(std::malloc(64));
  if(!binary->data){std::free(binary);return nullptr;}
  binary->size=64;std::memset(binary->data,int(tag),64);return binary;
}
extern "C" int xe_binary_native_validate(const void* opaque,unsigned tag) {
  const auto* binary=static_cast<const XeDxilShaderBinary*>(opaque);
  if(!binary || binary->size!=64)return 0;
  for(size_t i=0;i<binary->size;++i)if(binary->data[i]!=tag)return 0;
  return 1;
}
extern "C" void xe_binary_native_delete(void* binary) {
  xe_dxil_shader_binary_delete(static_cast<XeDxilShaderBinary*>(binary));
}

extern "C" void xe_binary_native_sizes(size_t* base,size_t* native) {
  *base=sizeof(xe::gpu::SpirvShader::SpirvTranslation);
  *native=sizeof(D3D12Shader::D3D12Translation);
}
