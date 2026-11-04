/* Device-interface glue only. Allocation policy and ownership are C89. */
#include "xenia/ui/vulkan/vulkan_texture_memory.h"
namespace xe {
namespace ui {
namespace vulkan {
xe_vk_heap* CreateTextureMemoryHeap(const VulkanDevice* device) {
  const auto& ifn = device->vulkan_instance()->functions();
  const auto& dfn = device->functions();
  VkPhysicalDeviceProperties properties;
  ifn.vkGetPhysicalDeviceProperties(device->physical_device(), &properties);
  xe_vk_heap_info info = {};
  info.device = device->device();
  ifn.vkGetPhysicalDeviceMemoryProperties(device->physical_device(), &info.memory);
  info.block_size = UINT64_C(64) << 20;
  info.max_blocks = properties.limits.maxMemoryAllocationCount < 256 ? properties.limits.maxMemoryAllocationCount : 256;
  info.max_spans = 65536;
  info.functions.allocate = dfn.vkAllocateMemory;
  info.functions.free = dfn.vkFreeMemory;
  info.functions.create_image = dfn.vkCreateImage;
  info.functions.destroy_image = dfn.vkDestroyImage;
  info.functions.create_buffer = dfn.vkCreateBuffer;
  info.functions.destroy_buffer = dfn.vkDestroyBuffer;
  info.functions.image_requirements = dfn.vkGetImageMemoryRequirements;
  info.functions.buffer_requirements = dfn.vkGetBufferMemoryRequirements;
  info.functions.bind_image = dfn.vkBindImageMemory;
  info.functions.bind_buffer = dfn.vkBindBufferMemory;
  if (device->extensions().ext_1_1_KHR_get_memory_requirements2 &&
      device->extensions().ext_1_1_KHR_dedicated_allocation) {
    info.dedicated_enabled = 1;
    info.functions.image_requirements2 = dfn.vkGetImageMemoryRequirements2;
    info.functions.buffer_requirements2 = dfn.vkGetBufferMemoryRequirements2;
  }
  if (device->extensions().ext_EXT_memory_budget &&
      device->vulkan_instance()->extensions().ext_1_1_KHR_get_physical_device_properties2) {
    VkPhysicalDeviceMemoryBudgetPropertiesEXT budget = {};
    budget.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;
    VkPhysicalDeviceMemoryProperties2 memory = {};
    memory.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
    memory.pNext = &budget;
    ifn.vkGetPhysicalDeviceMemoryProperties2(device->physical_device(), &memory);
    info.heap_limits_valid = 1;
    for (uint32_t i = 0; i < info.memory.memoryHeapCount; ++i) {
      info.heap_limits[i] = budget.heapBudget[i] > budget.heapUsage[i]
          ? budget.heapBudget[i] - budget.heapUsage[i] : 0;
    }
  }
  xe_vk_heap* heap = xe_vk_heap_create(&info);
  if (heap && xe_vk_heap_warm(heap) != VK_SUCCESS) {
    xe_vk_heap_destroy(heap);
    heap = nullptr;
  }
  return heap;
}
}
}
}
