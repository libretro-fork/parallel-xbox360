#include "core_vk_heap.h"
#include <stdlib.h>
#include <string.h>

typedef struct xe_vk_block {
   VkDeviceMemory memory;
   VkDeviceSize size;
   xe_gpu_span_arena arena;
   uint32_t type;
   int linear, isolated, fixed;
} xe_vk_block;
struct xe_vk_heap {
   xe_vk_heap_info info;
   xe_vk_block *blocks;
   xe_gpu_span_pool spans;
   VkDeviceSize reserved[VK_MAX_MEMORY_HEAPS];
};

xe_vk_heap *xe_vk_heap_create(const xe_vk_heap_info *info) {
   xe_vk_heap *heap;
   xe_gpu_span_node *nodes;
   size_t bytes;
   uint32_t i;
   if (!info || !info->device || !info->max_blocks || !info->max_spans ||
       !info->block_size || !info->memory.memoryTypeCount || info->memory.memoryTypeCount > VK_MAX_MEMORY_TYPES ||
       !info->memory.memoryHeapCount || info->memory.memoryHeapCount > VK_MAX_MEMORY_HEAPS ||
       !info->functions.allocate || !info->functions.free ||
       !info->functions.create_image || !info->functions.destroy_image ||
       !info->functions.create_buffer || !info->functions.destroy_buffer ||
       !info->functions.image_requirements || !info->functions.buffer_requirements ||
       !info->functions.bind_image || !info->functions.bind_buffer ||
       (info->dedicated_enabled && (!info->functions.image_requirements2 || !info->functions.buffer_requirements2))) return NULL;
   for (i = 0; i < info->memory.memoryTypeCount; ++i)
      if (info->memory.memoryTypes[i].heapIndex >= info->memory.memoryHeapCount) return NULL;
   bytes = (size_t)info->max_spans * sizeof(*nodes);
   if (bytes / sizeof(*nodes) != info->max_spans) return NULL;
   heap = (xe_vk_heap *)calloc(1, sizeof(*heap));
   if (!heap) return NULL;
   heap->info = *info;
   heap->blocks = (xe_vk_block *)calloc(info->max_blocks, sizeof(*heap->blocks));
   nodes = (xe_gpu_span_node *)malloc(bytes);
   if (!heap->blocks || !nodes || !xe_gpu_span_pool_init(&heap->spans, nodes, info->max_spans)) {
      free(nodes); free(heap->blocks); free(heap); return NULL;
   }
   return heap;
}
static void release_block(xe_vk_heap *heap, uint32_t index) {
   xe_vk_block *block = &heap->blocks[index];
   uint32_t memory_heap = heap->info.memory.memoryTypes[block->type].heapIndex;
   xe_gpu_span_arena_close(&block->arena);
   heap->info.functions.free(heap->info.device, block->memory, NULL);
   heap->reserved[memory_heap] -= block->size;
   memset(block, 0, sizeof(*block));
}
static int type_allowed(const xe_vk_heap *heap, uint32_t type) {
   VkMemoryPropertyFlags flags;
   uint32_t memory_heap;
   if (type >= heap->info.memory.memoryTypeCount) return 0;
   flags = heap->info.memory.memoryTypes[type].propertyFlags;
   memory_heap = heap->info.memory.memoryTypes[type].heapIndex;
   /* These resource paths enable neither protected/tile nor AMD coherent memory. */
   return !(flags & (VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT |
         VK_MEMORY_PROPERTY_PROTECTED_BIT | VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD |
         VK_MEMORY_PROPERTY_DEVICE_UNCACHED_BIT_AMD)) &&
         !(heap->info.memory.memoryHeaps[memory_heap].flags & VK_MEMORY_HEAP_TILE_MEMORY_BIT_QCOM);
}
static VkResult create_block(xe_vk_heap *heap, uint32_t type, int linear,
      VkDeviceSize size, VkImage image, VkBuffer buffer, int isolated, int fixed,
      uint32_t *out) {
   VkMemoryAllocateInfo info;
   VkMemoryDedicatedAllocateInfo dedicated;
   VkDeviceMemory memory = VK_NULL_HANDLE;
   VkResult result;
   uint32_t index, memory_heap;
   VkDeviceSize limit;
   xe_vk_block *block;
   if (type >= heap->info.memory.memoryTypeCount || !size || !heap->spans.available) return VK_ERROR_OUT_OF_HOST_MEMORY;
   if (!type_allowed(heap, type)) return VK_ERROR_FEATURE_NOT_PRESENT;
   memory_heap = heap->info.memory.memoryTypes[type].heapIndex;
   limit = heap->info.heap_limits_valid ? heap->info.heap_limits[memory_heap] : heap->info.memory.memoryHeaps[memory_heap].size;
   if (heap->reserved[memory_heap] > limit || size > limit - heap->reserved[memory_heap]) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
   for (index = 0; index < heap->info.max_blocks; ++index)
      if (!heap->blocks[index].memory) break;
   if (index == heap->info.max_blocks) return VK_ERROR_TOO_MANY_OBJECTS;
   memset(&info, 0, sizeof(info)); info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
   info.allocationSize = size; info.memoryTypeIndex = type;
   if (image || buffer) {
      memset(&dedicated, 0, sizeof(dedicated));
      dedicated.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
      dedicated.image = image; dedicated.buffer = buffer; info.pNext = &dedicated;
   }
   result = heap->info.functions.allocate(heap->info.device, &info, NULL, &memory);
   if (result != VK_SUCCESS) return result;
   block = &heap->blocks[index];
   if (!xe_gpu_span_arena_init(&block->arena, &heap->spans, size)) {
      heap->info.functions.free(heap->info.device, memory, NULL); return VK_ERROR_OUT_OF_HOST_MEMORY;
   }
   block->memory = memory; block->size = size; block->type = type;
   block->linear = linear; block->isolated = isolated; block->fixed = fixed;
   heap->reserved[memory_heap] += size; *out = index;
   return VK_SUCCESS;
}
VkResult xe_vk_heap_reserve(xe_vk_heap *heap, uint32_t type, int linear, VkDeviceSize bytes) {
   uint32_t index;
   if (!heap || !bytes) return VK_ERROR_INITIALIZATION_FAILED;
   return create_block(heap, type, linear != 0, bytes, VK_NULL_HANDLE, VK_NULL_HANDLE, 0, 1, &index);
}
static VkResult allocate_span(xe_vk_heap *heap, const VkMemoryRequirements *requirements,
      int linear, int dedicated, VkImage image, VkBuffer buffer, xe_vk_allocation *out) {
   uint32_t pass, type, index;
   VkMemoryPropertyFlags flags;
   VkDeviceSize block_size;
   VkResult result = VK_ERROR_OUT_OF_DEVICE_MEMORY;
   xe_vk_block *block;
   xe_gpu_span span;
   int isolated;
   if (!requirements->size || !requirements->alignment ||
       (requirements->alignment & (requirements->alignment - 1))) return VK_ERROR_INITIALIZATION_FAILED;
   for (pass = 0; pass < 2; ++pass) {
      for (type = 0; type < heap->info.memory.memoryTypeCount; ++type) {
         if (!(requirements->memoryTypeBits & (UINT32_C(1) << type))) continue;
         flags = heap->info.memory.memoryTypes[type].propertyFlags;
         if (!type_allowed(heap, type)) continue;
         if (((flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0) != (pass == 0)) continue;
         if (!dedicated) for (index = 0; index < heap->info.max_blocks; ++index) {
            block = &heap->blocks[index];
            if (!block->memory || block->type != type || block->linear != linear || block->isolated) continue;
            if (xe_gpu_span_take(&block->arena, requirements->size, requirements->alignment, &span)) {
               out->memory = block->memory; out->span = span; out->block = index; return VK_SUCCESS;
            }
         }
         isolated = dedicated;
         block_size = dedicated || requirements->size > heap->info.block_size
               ? requirements->size : heap->info.block_size;
         /* A fresh block needs its allocated node plus at most one tail node. */
         if (heap->spans.available < (uint32_t)(1 + (block_size != requirements->size))) return VK_ERROR_OUT_OF_HOST_MEMORY;
         result = create_block(heap, type, linear, block_size,
               dedicated ? image : VK_NULL_HANDLE, dedicated ? buffer : VK_NULL_HANDLE,
               isolated, 0, &index);
         if (result == VK_ERROR_OUT_OF_DEVICE_MEMORY && !isolated && block_size > requirements->size) {
            result = create_block(heap, type, linear, requirements->size,
                  VK_NULL_HANDLE, VK_NULL_HANDLE, 0, 0, &index);
         }
         if (result != VK_SUCCESS) continue;
         block = &heap->blocks[index];
         if (!xe_gpu_span_take(&block->arena, requirements->size, requirements->alignment, &span)) {
            release_block(heap, index); return VK_ERROR_OUT_OF_HOST_MEMORY;
         }
         out->memory = block->memory; out->span = span; out->block = index;
         return VK_SUCCESS;
      }
   }
   return result;
}
static int valid(const xe_vk_heap *heap, const xe_vk_allocation *allocation) {
   const xe_vk_block *block;
   const xe_gpu_span_node *node;
   if (!heap || !allocation || allocation->block >= heap->info.max_blocks ||
       allocation->span.slot >= heap->spans.capacity) return 0;
   block = &heap->blocks[allocation->block]; node = &heap->spans.nodes[allocation->span.slot];
   return block->memory && block->memory == allocation->memory && node->owner == &block->arena &&
         node->live && node->tag == allocation->span.tag && node->offset == allocation->span.offset && node->size == allocation->span.size;
}
static void free_span(xe_vk_heap *heap, const xe_vk_allocation *allocation) {
   uint32_t i;
   xe_vk_block *block = &heap->blocks[allocation->block];
   xe_gpu_span_return(&block->arena, &allocation->span);
   if (block->arena.used || block->fixed) return;
   if (!block->isolated) {
      /* Keep the largest warm empty block per type/class. Oversized ordinary
       * textures must also be reusable; only truly dedicated blocks retire. */
      for (i = 0; i < heap->info.max_blocks; ++i) {
         if (i == allocation->block || !heap->blocks[i].memory ||
             heap->blocks[i].isolated || heap->blocks[i].arena.used ||
             heap->blocks[i].type != block->type || heap->blocks[i].linear != block->linear) continue;
         if (heap->blocks[i].size >= block->size) {
            release_block(heap, allocation->block); return;
         }
         if (!heap->blocks[i].fixed) release_block(heap, i);
      }
      return;
   }
   release_block(heap, allocation->block);
}
VkResult xe_vk_heap_create_image(xe_vk_heap *heap, const VkImageCreateInfo *info,
      VkImage *image, xe_vk_allocation *allocation) {
   VkResult result;
   VkMemoryRequirements requirements;
   VkImageMemoryRequirementsInfo2 query;
   VkMemoryRequirements2 requirements2;
   VkMemoryDedicatedRequirements dedicated;
   if (!heap || !info || !image || !allocation) return VK_ERROR_INITIALIZATION_FAILED;
   *image = VK_NULL_HANDLE; memset(allocation, 0, sizeof(*allocation));
   if (info->flags & (VK_IMAGE_CREATE_SPARSE_BINDING_BIT | VK_IMAGE_CREATE_DISJOINT_BIT)) return VK_ERROR_FEATURE_NOT_PRESENT;
   memset(&dedicated, 0, sizeof(dedicated));
   result = heap->info.functions.create_image(heap->info.device, info, NULL, image);
   if (result != VK_SUCCESS) { *image = VK_NULL_HANDLE; return result; }
   if (heap->info.dedicated_enabled) {
      memset(&query, 0, sizeof(query)); query.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2; query.image = *image;
      memset(&requirements2, 0, sizeof(requirements2)); requirements2.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
      dedicated.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS; requirements2.pNext = &dedicated;
      heap->info.functions.image_requirements2(heap->info.device, &query, &requirements2);
      requirements = requirements2.memoryRequirements;
   } else heap->info.functions.image_requirements(heap->info.device, *image, &requirements);
   result = allocate_span(heap, &requirements, info->tiling == VK_IMAGE_TILING_LINEAR,
         dedicated.requiresDedicatedAllocation != 0, *image, VK_NULL_HANDLE, allocation);
   if (result == VK_SUCCESS) {
      result = heap->info.functions.bind_image(heap->info.device, *image, allocation->memory, allocation->span.offset);
   }
   if (result != VK_SUCCESS) {
      heap->info.functions.destroy_image(heap->info.device, *image, NULL);
      if (allocation->memory) free_span(heap, allocation);
      *image = VK_NULL_HANDLE; memset(allocation, 0, sizeof(*allocation));
   }
   return result;
}
VkResult xe_vk_heap_create_buffer(xe_vk_heap *heap, const VkBufferCreateInfo *info,
      VkBuffer *buffer, xe_vk_allocation *allocation) {
   VkResult result;
   VkMemoryRequirements requirements;
   VkBufferMemoryRequirementsInfo2 query;
   VkMemoryRequirements2 requirements2;
   VkMemoryDedicatedRequirements dedicated;
   if (!heap || !info || !buffer || !allocation) return VK_ERROR_INITIALIZATION_FAILED;
   *buffer = VK_NULL_HANDLE; memset(allocation, 0, sizeof(*allocation));
   if ((info->flags & VK_BUFFER_CREATE_SPARSE_BINDING_BIT) ||
       (info->usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)) return VK_ERROR_FEATURE_NOT_PRESENT;
   memset(&dedicated, 0, sizeof(dedicated));
   result = heap->info.functions.create_buffer(heap->info.device, info, NULL, buffer);
   if (result != VK_SUCCESS) { *buffer = VK_NULL_HANDLE; return result; }
   if (heap->info.dedicated_enabled) {
      memset(&query, 0, sizeof(query)); query.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2; query.buffer = *buffer;
      memset(&requirements2, 0, sizeof(requirements2)); requirements2.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
      dedicated.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS; requirements2.pNext = &dedicated;
      heap->info.functions.buffer_requirements2(heap->info.device, &query, &requirements2);
      requirements = requirements2.memoryRequirements;
   } else heap->info.functions.buffer_requirements(heap->info.device, *buffer, &requirements);
   result = allocate_span(heap, &requirements, 1, dedicated.requiresDedicatedAllocation != 0,
         VK_NULL_HANDLE, *buffer, allocation);
   if (result == VK_SUCCESS) {
      result = heap->info.functions.bind_buffer(heap->info.device, *buffer, allocation->memory, allocation->span.offset);
   }
   if (result != VK_SUCCESS) {
      heap->info.functions.destroy_buffer(heap->info.device, *buffer, NULL);
      if (allocation->memory) free_span(heap, allocation);
      *buffer = VK_NULL_HANDLE; memset(allocation, 0, sizeof(*allocation));
   }
   return result;
}
int xe_vk_heap_destroy_image(xe_vk_heap *heap, VkImage image, const xe_vk_allocation *allocation) {
   if (!image || !valid(heap, allocation)) return 0;
   heap->info.functions.destroy_image(heap->info.device, image, NULL);
   free_span(heap, allocation); return 1;
}
int xe_vk_heap_destroy_buffer(xe_vk_heap *heap, VkBuffer buffer, const xe_vk_allocation *allocation) {
   if (!buffer || !valid(heap, allocation)) return 0;
   heap->info.functions.destroy_buffer(heap->info.device, buffer, NULL);
   free_span(heap, allocation); return 1;
}
int xe_vk_heap_destroy(xe_vk_heap *heap) {
   uint32_t i;
   if (!heap) return 1;
   for (i = 0; i < heap->info.max_blocks; ++i)
      if (heap->blocks[i].memory && heap->blocks[i].arena.used) return 0;
   for (i = 0; i < heap->info.max_blocks; ++i)
      if (heap->blocks[i].memory) release_block(heap, i);
   free(heap->spans.nodes); free(heap->blocks); free(heap); return 1;
}

VkResult xe_vk_heap_warm(xe_vk_heap *heap) {
   VkImageCreateInfo image_info;
   VkBufferCreateInfo buffer_info;
   VkImage image;
   VkBuffer buffer;
   xe_vk_allocation allocation;
   VkResult result;
   if (!heap) return VK_ERROR_INITIALIZATION_FAILED;
   memset(&image_info, 0, sizeof(image_info));
   image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
   image_info.imageType = VK_IMAGE_TYPE_2D;
   image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
   image_info.extent.width = image_info.extent.height = 16;
   image_info.extent.depth = 1; image_info.mipLevels = image_info.arrayLayers = 1;
   image_info.samples = VK_SAMPLE_COUNT_1_BIT;
   image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
   image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
   result = xe_vk_heap_create_image(heap, &image_info, &image, &allocation);
   if (result != VK_SUCCESS) return result;
   xe_vk_heap_destroy_image(heap, image, &allocation);
   memset(&buffer_info, 0, sizeof(buffer_info));
   buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
   buffer_info.size = 4096; buffer_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
   result = xe_vk_heap_create_buffer(heap, &buffer_info, &buffer, &allocation);
   if (result == VK_SUCCESS) xe_vk_heap_destroy_buffer(heap, buffer, &allocation);
   return result;
}
