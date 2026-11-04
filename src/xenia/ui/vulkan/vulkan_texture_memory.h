#ifndef XENIA_UI_VULKAN_TEXTURE_MEMORY_H_
#define XENIA_UI_VULKAN_TEXTURE_MEMORY_H_
#include "xenia/ui/vulkan/vulkan_device.h"
#include "libretro/core_vk_heap.h"
namespace xe {
namespace ui {
namespace vulkan {
xe_vk_heap* CreateTextureMemoryHeap(const VulkanDevice* device);
}
}
}
#endif
