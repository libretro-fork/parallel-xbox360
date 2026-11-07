/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_VULKAN_VULKAN_SHADER_H_
#define XENIA_GPU_VULKAN_VULKAN_SHADER_H_

#include <cstdint>

#include "xenia/gpu/spirv_shader.h"
#include "xenia/gpu/xenos.h"
#include "xenia/ui/vulkan/vulkan_device.h"

namespace xe {
namespace gpu {
namespace vulkan {

class VulkanShader : public SpirvShader {
 public:
  class VulkanTranslation : public SpirvTranslation {
   public:
    explicit VulkanTranslation(VulkanShader& shader, uint64_t modification)
        : SpirvTranslation(shader, modification) {}
    ~VulkanTranslation() override;

    // The translation publication also publishes its immutable module.
    VkShaderModule shader_module() const {
      return is_translated() && is_valid() ? shader_module_ : VK_NULL_HANDLE;
    }

   protected:
    bool PrepareForPublication() override;

   private:
    VkShaderModule shader_module_ = VK_NULL_HANDLE;
  };

  explicit VulkanShader(const ui::vulkan::VulkanDevice* vulkan_device,
                        xenos::ShaderType shader_type, uint64_t ucode_data_hash,
                        const uint32_t* ucode_dwords, size_t ucode_dword_count,
                        std::endian ucode_source_endian = std::endian::big);

 protected:
  Translation* CreateTranslationInstance(uint64_t modification) override;

 private:
  const ui::vulkan::VulkanDevice* vulkan_device_;


};

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_VULKAN_VULKAN_SHADER_H_
