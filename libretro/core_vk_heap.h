/* Texture-cache device-memory ownership; C89, externally serialized. */
#ifndef XE_CORE_VK_HEAP_H
#define XE_CORE_VK_HEAP_H
#ifndef VULKAN_CORE_H_
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan_core.h>
#endif
#include "core_gpu_spans.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xe_vk_heap xe_vk_heap;
typedef struct xe_vk_heap_functions {
   PFN_vkAllocateMemory allocate;
   PFN_vkFreeMemory free;
   PFN_vkCreateImage create_image;
   PFN_vkDestroyImage destroy_image;
   PFN_vkCreateBuffer create_buffer;
   PFN_vkDestroyBuffer destroy_buffer;
   PFN_vkGetImageMemoryRequirements image_requirements;
   PFN_vkGetBufferMemoryRequirements buffer_requirements;
   PFN_vkGetImageMemoryRequirements2 image_requirements2;
   PFN_vkGetBufferMemoryRequirements2 buffer_requirements2;
   PFN_vkBindImageMemory bind_image;
   PFN_vkBindBufferMemory bind_buffer;
} xe_vk_heap_functions;
typedef struct xe_vk_heap_info {
   VkDevice device;
   VkPhysicalDeviceMemoryProperties memory;
   xe_vk_heap_functions functions;
   VkDeviceSize block_size;
   VkDeviceSize heap_limits[VK_MAX_MEMORY_HEAPS];
   uint32_t max_blocks, max_spans;
   int dedicated_enabled, heap_limits_valid;
} xe_vk_heap_info;
typedef struct xe_vk_allocation {
   VkDeviceMemory memory;
   xe_gpu_span span;
   uint32_t block;
} xe_vk_allocation;
xe_vk_heap *xe_vk_heap_create(const xe_vk_heap_info *info);
/* Cold reservation. type must be permitted by actual resource requirements.
 * Fixed blocks survive until shutdown. linear separates buffers/linear images
 * from optimal images; the two classes never share VkDeviceMemory. */
VkResult xe_vk_heap_reserve(xe_vk_heap *heap, uint32_t type, int linear, VkDeviceSize bytes);
VkResult xe_vk_heap_create_image(xe_vk_heap *heap, const VkImageCreateInfo *info,
      VkImage *image, xe_vk_allocation *allocation);
VkResult xe_vk_heap_create_buffer(xe_vk_heap *heap, const VkBufferCreateInfo *info,
      VkBuffer *buffer, xe_vk_allocation *allocation);
/* Cold probes reserve reusable blocks for the device's real image/buffer types. */
VkResult xe_vk_heap_warm(xe_vk_heap *heap);
/* Renderer must finish GPU use first. No allocation/free function waits. */
int xe_vk_heap_destroy_image(xe_vk_heap *heap, VkImage image, const xe_vk_allocation *allocation);
int xe_vk_heap_destroy_buffer(xe_vk_heap *heap, VkBuffer buffer, const xe_vk_allocation *allocation);
/* Refuses teardown while live spans remain. */
int xe_vk_heap_destroy(xe_vk_heap *heap);
#ifdef __cplusplus
}
#endif
#endif
