/* Test-only device dispatch. The production allocator translation unit is built
 * unchanged; no native device, loader or GPU is involved. */
#ifndef XENIA_UI_VULKAN_VULKAN_DEVICE_H_
#define XENIA_UI_VULKAN_VULKAN_DEVICE_H_
#include <vulkan/vulkan_core.h>
namespace xe { namespace ui { namespace vulkan {
class VulkanDevice {
 public:
  struct Functions {
    PFN_vkCreateDescriptorPool vkCreateDescriptorPool;
    PFN_vkDestroyDescriptorPool vkDestroyDescriptorPool;
    PFN_vkAllocateDescriptorSets vkAllocateDescriptorSets;
  };
  explicit VulkanDevice(const Functions& functions) : functions_(functions) {}
  VkDevice device() const { return (VkDevice)(uintptr_t)1; }
  const Functions& functions() const { return functions_; }
 private:
  Functions functions_;
};
}}}
#endif
