/* Stub Vulkan driver checks ownership, exact binding and failure rollback. C89. */
#include <stdio.h>
#include <string.h>
#include "core_vk_heap.h"
typedef struct resource { int active, dedicated; VkDeviceSize size, alignment; uint32_t bits; VkDeviceMemory memory; } resource;
typedef struct memory { int active; VkDeviceSize size; uint32_t type; VkImage image; VkBuffer buffer; } memory;
static resource resources[128];
static memory memories[32];
static unsigned allocations, frees, live_resources, bad;
static int dedicated_next, fail_bind, fail_device_type, fail_create;
static resource *new_resource(VkDeviceSize size) {
   unsigned i;
   for (i = 0; i < 128; ++i) if (!resources[i].active) {
      memset(&resources[i], 0, sizeof(resources[i]));
      resources[i].active = 1; resources[i].dedicated = dedicated_next;
      resources[i].size = size; resources[i].alignment = 256; resources[i].bits = 3;
      ++live_resources; return &resources[i];
   }
   return NULL;
}
static VKAPI_ATTR VkResult VKAPI_CALL allocate(VkDevice device, const VkMemoryAllocateInfo *info,
      const VkAllocationCallbacks *callbacks, VkDeviceMemory *out) {
   const VkMemoryDedicatedAllocateInfo *dedicated;
   unsigned i;
   (void)device; (void)callbacks;
   if (fail_device_type && info->memoryTypeIndex == 0) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
   for (i = 0; i < 32; ++i) if (!memories[i].active) {
      memset(&memories[i], 0, sizeof(memories[i]));
      memories[i].active = 1; memories[i].size = info->allocationSize; memories[i].type = info->memoryTypeIndex;
      if (info->pNext) {
         dedicated = (const VkMemoryDedicatedAllocateInfo *)info->pNext;
         if (dedicated->sType != VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO) ++bad;
         memories[i].image = dedicated->image; memories[i].buffer = dedicated->buffer;
      }
      *out = (VkDeviceMemory)&memories[i]; ++allocations; return VK_SUCCESS;
   }
   return VK_ERROR_TOO_MANY_OBJECTS;
}
static VKAPI_ATTR void VKAPI_CALL free_memory(VkDevice device, VkDeviceMemory handle, const VkAllocationCallbacks *callbacks) {
   memory *m = (memory *)handle;
   unsigned i;
   (void)device; (void)callbacks;
   if (!m->active) ++bad;
   for (i = 0; i < 128; ++i) if (resources[i].active && resources[i].memory == handle) ++bad;
   m->active = 0; ++frees;
}
static VKAPI_ATTR VkResult VKAPI_CALL create_image(VkDevice device, const VkImageCreateInfo *info,
      const VkAllocationCallbacks *callbacks, VkImage *out) {
   resource *r;
   (void)device; (void)callbacks;
   if (fail_create) { *out = (VkImage)(uintptr_t)1; return VK_ERROR_OUT_OF_HOST_MEMORY; }
   r = new_resource(info->extent.width); if (!r) return VK_ERROR_OUT_OF_HOST_MEMORY;
   *out = (VkImage)r; return VK_SUCCESS;
}
static VKAPI_ATTR VkResult VKAPI_CALL create_buffer(VkDevice device, const VkBufferCreateInfo *info,
      const VkAllocationCallbacks *callbacks, VkBuffer *out) {
   resource *r;
   (void)device; (void)callbacks;
   if (fail_create) { *out = (VkBuffer)(uintptr_t)1; return VK_ERROR_OUT_OF_HOST_MEMORY; }
   r = new_resource(info->size); if (!r) return VK_ERROR_OUT_OF_HOST_MEMORY;
   *out = (VkBuffer)r; return VK_SUCCESS;
}
static void destroy(resource *r) { if (!r->active) ++bad; r->active = 0; --live_resources; }
static VKAPI_ATTR void VKAPI_CALL destroy_image(VkDevice device, VkImage image, const VkAllocationCallbacks *callbacks) {
   (void)device; (void)callbacks; destroy((resource *)image);
}
static VKAPI_ATTR void VKAPI_CALL destroy_buffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks *callbacks) {
   (void)device; (void)callbacks; destroy((resource *)buffer);
}
static void requirements(resource *r, VkMemoryRequirements *out) {
   out->size = r->size; out->alignment = r->alignment; out->memoryTypeBits = r->bits;
}
static VKAPI_ATTR void VKAPI_CALL image_requirements(VkDevice device, VkImage image, VkMemoryRequirements *out) {
   (void)device; requirements((resource *)image, out);
}
static VKAPI_ATTR void VKAPI_CALL buffer_requirements(VkDevice device, VkBuffer buffer, VkMemoryRequirements *out) {
   (void)device; requirements((resource *)buffer, out);
}
static void requirements2(resource *r, VkMemoryRequirements2 *out) {
   VkMemoryDedicatedRequirements *d = (VkMemoryDedicatedRequirements *)out->pNext;
   requirements(r, &out->memoryRequirements);
   if (!d || d->sType != VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS) { ++bad; return; }
   d->requiresDedicatedAllocation = r->dedicated ? VK_TRUE : VK_FALSE;
}
static VKAPI_ATTR void VKAPI_CALL image_requirements2(VkDevice device, const VkImageMemoryRequirementsInfo2 *info, VkMemoryRequirements2 *out) {
   (void)device; requirements2((resource *)info->image, out);
}
static VKAPI_ATTR void VKAPI_CALL buffer_requirements2(VkDevice device, const VkBufferMemoryRequirementsInfo2 *info, VkMemoryRequirements2 *out) {
   (void)device; requirements2((resource *)info->buffer, out);
}
static VkResult bind(resource *r, VkDeviceMemory handle, VkDeviceSize offset, int image) {
   memory *m = (memory *)handle;
   if (!m->active || offset % r->alignment || offset > m->size || r->size > m->size - offset ||
       !(r->bits & (UINT32_C(1) << m->type))) ++bad;
   if (r->dedicated && (offset || (image ? m->image != (VkImage)r : m->buffer != (VkBuffer)r))) ++bad;
   if (fail_bind) return VK_ERROR_DEVICE_LOST;
   r->memory = handle; return VK_SUCCESS;
}
static VKAPI_ATTR VkResult VKAPI_CALL bind_image(VkDevice device, VkImage image, VkDeviceMemory handle, VkDeviceSize offset) {
   (void)device; return bind((resource *)image, handle, offset, 1);
}
static VKAPI_ATTR VkResult VKAPI_CALL bind_buffer(VkDevice device, VkBuffer buffer, VkDeviceMemory handle, VkDeviceSize offset) {
   (void)device; return bind((resource *)buffer, handle, offset, 0);
}
static xe_vk_heap_info config(void) {
   xe_vk_heap_info info;
   memset(&info, 0, sizeof(info));
   info.device = (VkDevice)(uintptr_t)1;
   info.block_size = 4096; info.max_blocks = 8; info.max_spans = 128; info.dedicated_enabled = 1;
   info.memory.memoryHeapCount = 1; info.memory.memoryHeaps[0].size = 65536;
   info.memory.memoryTypeCount = 2;
   info.memory.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
   info.memory.memoryTypes[1].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
   info.functions.allocate = allocate; info.functions.free = free_memory;
   info.functions.create_image = create_image; info.functions.destroy_image = destroy_image;
   info.functions.create_buffer = create_buffer; info.functions.destroy_buffer = destroy_buffer;
   info.functions.image_requirements = image_requirements; info.functions.buffer_requirements = buffer_requirements;
   info.functions.image_requirements2 = image_requirements2; info.functions.buffer_requirements2 = buffer_requirements2;
   info.functions.bind_image = bind_image; info.functions.bind_buffer = bind_buffer;
   return info;
}
int main(void) {
   xe_vk_heap_info info = config();
   xe_vk_heap *heap;
   VkImageCreateInfo image_info;
   VkBufferCreateInfo buffer_info;
   VkImage image, replacement;
   VkBuffer buffer;
   xe_vk_allocation allocation, buffer_allocation, stale, other;
   unsigned i, before;
   memset(&image_info, 0, sizeof(image_info)); image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
   image_info.extent.width = 768; image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
   memset(&buffer_info, 0, sizeof(buffer_info)); buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
   buffer_info.size = 512; buffer_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
   heap = xe_vk_heap_create(&info); if (!heap) return 1;
   if (xe_vk_heap_warm(heap) != VK_SUCCESS) return 2;
   fail_create = 1;
   if (xe_vk_heap_create_image(heap, &image_info, &image, &allocation) != VK_ERROR_OUT_OF_HOST_MEMORY || image || allocation.memory ||
       xe_vk_heap_create_buffer(heap, &buffer_info, &buffer, &allocation) != VK_ERROR_OUT_OF_HOST_MEMORY || buffer || allocation.memory) return 22;
   fail_create = 0;
   before = allocations;
   for (i = 0; i < 10000; ++i) {
      if (xe_vk_heap_create_image(heap, &image_info, &image, &allocation) ||
          xe_vk_heap_create_buffer(heap, &buffer_info, &buffer, &buffer_allocation)) return 3;
      if (allocation.memory == buffer_allocation.memory || xe_vk_heap_destroy(heap)) return 4;
      stale = allocation;
      if (!xe_vk_heap_destroy_image(heap, image, &allocation) || xe_vk_heap_destroy_image(heap, image, &allocation)) return 5;
      if (xe_vk_heap_create_image(heap, &image_info, &replacement, &other) ||
          xe_vk_heap_destroy_image(heap, replacement, &stale) ||
          !xe_vk_heap_destroy_image(heap, replacement, &other) ||
          !xe_vk_heap_destroy_buffer(heap, buffer, &buffer_allocation)) return 6;
   }
   if (allocations != before) return 7;
   dedicated_next = 1;
   if (xe_vk_heap_create_image(heap, &image_info, &image, &allocation) ||
       !xe_vk_heap_destroy_image(heap, image, &allocation) || frees != 1) return 8;
   fail_bind = 1;
   if (xe_vk_heap_create_image(heap, &image_info, &image, &allocation) != VK_ERROR_DEVICE_LOST || image || allocation.memory) return 9;
   if (xe_vk_heap_create_buffer(heap, &buffer_info, &buffer, &allocation) != VK_ERROR_DEVICE_LOST || buffer || allocation.memory) return 15;
   fail_bind = dedicated_next = 0;
   before = allocations; image_info.extent.width = 10000;
   for (i = 0; i < 2; ++i) {
      if (xe_vk_heap_create_image(heap, &image_info, &image, &allocation) ||
          !xe_vk_heap_destroy_image(heap, image, &allocation)) return 20;
   }
   if (allocations != before + 1) return 21;
   image_info.extent.width = 768;
   if (!xe_vk_heap_destroy(heap) || live_resources || allocations != frees || bad) return 10;
   info = config(); info.dedicated_enabled = 0; fail_device_type = 1;
   heap = xe_vk_heap_create(&info); if (!heap) return 11;
   if (xe_vk_heap_create_buffer(heap, &buffer_info, &buffer, &allocation) ||
       ((memory *)allocation.memory)->type != 1 ||
       !xe_vk_heap_destroy_buffer(heap, buffer, &allocation) || !xe_vk_heap_destroy(heap)) return 12;
   fail_device_type = 0;
   info = config(); info.memory.memoryTypes[0].propertyFlags |= VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD;
   heap = xe_vk_heap_create(&info); if (!heap) return 16;
   if (xe_vk_heap_create_image(heap, &image_info, &image, &allocation) ||
       ((memory *)allocation.memory)->type != 1 ||
       !xe_vk_heap_destroy_image(heap, image, &allocation) || !xe_vk_heap_destroy(heap)) return 17;
   info = config(); info.memory.memoryHeaps[0].flags |= VK_MEMORY_HEAP_TILE_MEMORY_BIT_QCOM;
   heap = xe_vk_heap_create(&info); if (!heap) return 18;
   if (xe_vk_heap_create_image(heap, &image_info, &image, &allocation) == VK_SUCCESS || image ||
       !xe_vk_heap_destroy(heap)) return 19;
   info = config(); info.heap_limits_valid = 1; info.heap_limits[0] = 128;
   heap = xe_vk_heap_create(&info); if (!heap) return 13;
   if (xe_vk_heap_create_image(heap, &image_info, &image, &allocation) == VK_SUCCESS || image || allocation.memory ||
       !xe_vk_heap_destroy(heap) || bad || live_resources || allocations != frees) return 14;
   info = config(); info.max_spans = 2;
   heap = xe_vk_heap_create(&info); if (!heap || xe_vk_heap_warm(heap)) return 23;
   before = allocations;
   if (xe_vk_heap_create_image(heap, &image_info, &image, &allocation) != VK_ERROR_OUT_OF_HOST_MEMORY ||
       image || allocation.memory || allocations != before || !xe_vk_heap_destroy(heap)) return 24;
   info = config(); info.max_blocks = 1;
   heap = xe_vk_heap_create(&info); if (!heap) return 25;
   if (xe_vk_heap_warm(heap) != VK_ERROR_TOO_MANY_OBJECTS || !xe_vk_heap_destroy(heap) ||
       bad || live_resources || allocations != frees) return 26;
   puts("PASS: Vulkan block reuse, no backing growth in 10000 warm cycles, dedicated chains, class separation, rollback, types and limits");
   return 0;
}
