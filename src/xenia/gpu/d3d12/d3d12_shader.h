/* Native D3D12 bytecode lifetime glue around C89 publication. BSD license. */
#ifndef XENIA_GPU_D3D12_D3D12_SHADER_H_
#define XENIA_GPU_D3D12_D3D12_SHADER_H_
#include "libretro/core_binary_slot.h"
#include "xenia/gpu/spirv_shader.h"
#include "xenia/gpu/dxil/shader_module.h"
namespace xe {
namespace gpu {
namespace d3d12 {
class D3D12Shader : public SpirvShader {
 public:
  using SpirvShader::SpirvShader;
  class D3D12Translation : public SpirvTranslation {
   public:
    D3D12Translation(D3D12Shader& shader, uint64_t modification)
        : SpirvTranslation(shader, modification) { xe_binary_slot_init(&native_); }
    ~D3D12Translation() override {
      xe_dxil_shader_binary_delete(
          static_cast<XeDxilShaderBinary*>(xe_binary_slot_take(&native_)));
    }
    xe_binary_snapshot native_snapshot() const { return xe_binary_slot_read(&native_); }
    bool PublishNativeBinary(XeDxilShaderBinary* binary) {
      return xe_binary_slot_publish(&native_, binary) != 0;
    }
   private:
    xe_binary_slot native_;
  };
 protected:
  Translation* CreateTranslationInstance(uint64_t modification) override {
    return new D3D12Translation(*this, modification);
  }
};
}  // namespace d3d12
}  // namespace gpu
}  // namespace xe
#endif
