/* Immutable whole-buffer source descriptors, and checked rebased views.
 * Single GPU owner; native resources must outlive the bank. MSVC C89. */
#ifndef XE_CORE_VK_SOURCE_VIEWS_H
#define XE_CORE_VK_SOURCE_VIEWS_H
#include <stdint.h>
#include <string.h>
#include <retro_inline.h>
#ifndef VULKAN_CORE_H_
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan_core.h>
#endif
#define XE_VK_SOURCE_BANK_CAPACITY 32
typedef struct xe_vk_source_functions {
  PFN_vkCreateDescriptorPool create_pool;
  PFN_vkDestroyDescriptorPool destroy_pool;
  PFN_vkAllocateDescriptorSets allocate_sets;
  PFN_vkUpdateDescriptorSets update_sets;
} xe_vk_source_functions;
typedef struct xe_vk_source_bank {
  VkDescriptorPool pool;
  VkDescriptorSet sets[XE_VK_SOURCE_BANK_CAPACITY];
  VkBuffer buffers[XE_VK_SOURCE_BANK_CAPACITY];
  uint64_t size;
  uint32_t count;
} xe_vk_source_bank;
typedef VkBuffer (*xe_vk_source_buffer_fn)(void *, uint32_t);
static INLINE void xe_vk_source_bank_close(xe_vk_source_bank *bank,
    VkDevice device, const xe_vk_source_functions *fn) {
  if (bank->pool) fn->destroy_pool(device, bank->pool, NULL);
  memset(bank, 0, sizeof(*bank));
}
/* Optional cold setup. Failure publishes no sets; callers keep transient views.
 * Only actual accesses need resident sparse backing, not the descriptor extent. */
static INLINE int xe_vk_source_bank_init(xe_vk_source_bank *bank,
    VkDevice device, const xe_vk_source_functions *fn, VkDescriptorSetLayout layout,
    uint32_t count, uint64_t size, uint64_t max_range,
    xe_vk_source_buffer_fn buffer, void *context) {
  VkDescriptorPool pool = VK_NULL_HANDLE;
  VkDescriptorPoolSize pool_size;
  VkDescriptorPoolCreateInfo pool_info;
  VkDescriptorSetAllocateInfo allocate_info;
  VkDescriptorSetLayout layouts[XE_VK_SOURCE_BANK_CAPACITY];
  VkDescriptorBufferInfo buffers[XE_VK_SOURCE_BANK_CAPACITY];
  VkWriteDescriptorSet writes[XE_VK_SOURCE_BANK_CAPACITY];
  uint32_t i;
  if (!fn || !fn->create_pool || !fn->destroy_pool || !fn->allocate_sets || !fn->update_sets ||
      bank->pool || bank->count || !count || count > XE_VK_SOURCE_BANK_CAPACITY ||
      !size || size > max_range || !layout || !buffer) return 0;
  for (i = 0; i < count; ++i) {
    bank->buffers[i] = buffer(context, i);
    if (!bank->buffers[i]) { memset(bank, 0, sizeof(*bank)); return 0; }
    layouts[i] = layout;
  }
  memset(&pool_info, 0, sizeof(pool_info));
  pool_size.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; pool_size.descriptorCount = count;
  pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  pool_info.maxSets = count; pool_info.poolSizeCount = 1; pool_info.pPoolSizes = &pool_size;
  if (fn->create_pool(device, &pool_info, NULL, &pool) != VK_SUCCESS) {
    memset(bank, 0, sizeof(*bank)); return 0;
  }
  bank->pool = pool;
  memset(&allocate_info, 0, sizeof(allocate_info));
  allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocate_info.descriptorPool = pool; allocate_info.descriptorSetCount = count;
  allocate_info.pSetLayouts = layouts;
  if (fn->allocate_sets(device, &allocate_info, bank->sets) != VK_SUCCESS) {
    xe_vk_source_bank_close(bank, device, fn); return 0;
  }
  for (i = 0; i < count; ++i) {
    buffers[i].buffer = bank->buffers[i]; buffers[i].offset = 0; buffers[i].range = size;
    memset(writes + i, 0, sizeof(writes[i]));
    writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[i].dstSet = bank->sets[i]; writes[i].descriptorCount = 1;
    writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[i].pBufferInfo = buffers + i;
  }
  fn->update_sets(device, count, writes, 0, NULL);
  bank->size = size; bank->count = count; return 1;
}
static INLINE VkDescriptorSet xe_vk_source_bank_get(const xe_vk_source_bank *bank,
    uint32_t index, VkBuffer buffer, uint64_t size) {
  if (index >= bank->count || bank->buffers[index] != buffer || size != bank->size)
    return VK_NULL_HANDLE;
  return bank->sets[index];
}
typedef struct xe_gpu_source_view {
  uint64_t descriptor_offset, descriptor_range;
  uint32_t shader_offset;
  int persistent;
} xe_gpu_source_view;
/* Move the source origin from the descriptor to the existing shader byte-offset
 * constant. The kernel adds this AFTER scaled tiling. No data is moved. */
static INLINE int xe_gpu_source_view_plan(uint64_t capacity, uint64_t offset,
    uint64_t length, uint64_t max_range, uint64_t alignment,
    int persistent_available, xe_gpu_source_view *out) {
  xe_gpu_source_view view;
  uint64_t padded;
  if (!out || !length || !alignment || (alignment & (alignment - 1)) ||
      (offset & 15) || offset > capacity ||
      length > capacity - offset || length > UINT64_MAX - 15) return 0;
  padded = (length + 15) & ~UINT64_C(15);
  if (padded > capacity - offset) return 0;
  view.persistent = persistent_available && capacity <= max_range && offset <= UINT32_MAX &&
      padded - 1 <= UINT32_MAX - offset;
  if (view.persistent) {
    view.descriptor_offset = 0; view.descriptor_range = capacity;
    view.shader_offset = (uint32_t)offset;
  } else {
    if ((offset & (alignment - 1)) || padded > max_range || padded - 1 > UINT32_MAX) return 0;
    view.descriptor_offset = offset; view.descriptor_range = padded; view.shader_offset = 0;
  }
  *out = view; return 1;
}
#endif
