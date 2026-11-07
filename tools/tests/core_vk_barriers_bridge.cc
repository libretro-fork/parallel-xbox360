/* Thin ABI glue only; C89 tests own the trace, allocation hooks and Vulkan mocks. */
#include <new>
#include "xenia/gpu/vulkan/deferred_command_buffer.h"
using Commands = xe::gpu::vulkan::DeferredCommandBuffer;
extern "C" void* xe_test_vk_new(size_t size) { return new Commands(size); }
extern "C" int xe_test_vk_try_new() {
  try { Commands commands(24); return 1; } catch (const std::bad_alloc&) { return 0; }
}
extern "C" void xe_test_vk_delete(void* p) { delete static_cast<Commands*>(p); }
extern "C" void xe_test_vk_buffer(void* p, unsigned i) {
  static_cast<Commands*>(p)->PushBufferMemoryBarrier((VkBuffer)(uintptr_t)1, 64, 128,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
      i + 1, i + 2, 3, 7, false);
}
extern "C" void xe_test_vk_image(void* p, unsigned i) {
  VkImageSubresourceRange r = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  static_cast<Commands*>(p)->PushImageMemoryBarrier((VkImage)(uintptr_t)1, r,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
      i + 1, i + 2,
      i & 1 ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL,
      i & 1 ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 3, 7, false);
}
extern "C" int xe_test_vk_submit(void* p) {
  try { static_cast<Commands*>(p)->SubmitBarriers(); return 1; }
  catch (const std::bad_alloc&) { return 0; }
}
extern "C" void xe_test_vk_marker(void* p, unsigned value) {
  static_cast<Commands*>(p)->CmdVkSetStencilReference(VK_STENCIL_FACE_FRONT_AND_BACK, value);
}
extern "C" void xe_test_vk_copy(void* p, const VkMemoryBarrier* m,
    const VkBufferMemoryBarrier* b, const VkImageMemoryBarrier* im) {
  static_cast<Commands*>(p)->CmdVkPipelineBarrier(1, 2, VK_DEPENDENCY_BY_REGION_BIT, 1, m, 1, b, 1, im);
}
extern "C" void xe_test_vk_execute(void* p, PFN_vkCmdPipelineBarrier barrier, PFN_vkCmdSetStencilReference marker) {
  xe::ui::vulkan::VulkanDevice::Functions dfn = {};
  xe::ui::vulkan::VulkanInstance::Functions ifn = {};
  dfn.vkCmdPipelineBarrier = barrier; dfn.vkCmdSetStencilReference = marker;
  static_cast<Commands*>(p)->Execute((VkCommandBuffer)(uintptr_t)123, dfn, ifn);
}
extern "C" void xe_test_vk_reset(void* p) { static_cast<Commands*>(p)->Reset(); }
