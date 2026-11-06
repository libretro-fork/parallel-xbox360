/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_VULKAN_LINKED_TYPE_DESCRIPTOR_SET_ALLOCATOR_H_
#define XENIA_UI_VULKAN_LINKED_TYPE_DESCRIPTOR_SET_ALLOCATOR_H_

#include <cstdint>
#include <new>

#include "xenia/base/assert.h"
#include "xenia/ui/vulkan/vulkan_device.h"
#include "libretro/core_vk_pool_owner.h"
#include "libretro/core_vk_pool_search.h"

namespace xe {
namespace ui {
namespace vulkan {

// Allocates multiple descriptors of in descriptor set layouts consisting of
// descriptors of types specified during initialization.
//
// "LinkedType" means that the allocator is designed for allocating descriptor
// sets containing descriptors of multiple types together - for instance, it
// will mark the entire page as full even if no space is left in it for just one
// of the descriptor types (not all at once).
//
// The primary usage scenario for this kind of an allocator is allocating image
// and sampler descriptors in a single descriptor set if they both are actually
// used in one. It is expected that the ratio of the numbers of descriptors per
// type specified during the initialization will roughly correspond to the ratio
// of the numbers of descriptors that will actually be allocated. For instance,
// if there are approximately 2 images for each 1 sampler, it's recommended to
// make the image count per page twice the sampler count per page.
//
// If some allocations use just one type, and some use just another, completely
// independently, it's preferable to use separate allocators rather than a
// single one.
//
// This allocator is also suitable for allocating variable-length descriptor
// sets containing descriptors of just a single type.
//
// There's no way to free these descriptors within the allocator object itself,
// per-layout free lists should be used externally.
class LinkedTypeDescriptorSetAllocator {
 public:
  // Multiple descriptor sizes for the same descriptor type, and zero sizes, are
  // not allowed. The sizes are borrowed and must outlive this allocator;
  // the core passes static descriptor tables.
  explicit LinkedTypeDescriptorSetAllocator(
      const VulkanDevice* const vulkan_device,
      const VkDescriptorPoolSize* const descriptor_sizes,
      const uint32_t descriptor_size_count,
      const uint32_t descriptor_sets_per_page)
      : vulkan_device_(vulkan_device),
        descriptor_pool_sizes_(descriptor_sizes),
        descriptor_pool_size_count_(descriptor_size_count),
        descriptor_sets_per_page_(descriptor_sets_per_page) {
    assert_not_null(vulkan_device);
    assert_not_zero(descriptor_size_count);
    assert_not_zero(descriptor_sets_per_page_);
#ifndef NDEBUG
    for (uint32_t i = 0; i < descriptor_size_count; ++i) {
      const VkDescriptorPoolSize& descriptor_size = descriptor_sizes[i];
      assert_not_zero(descriptor_size.descriptorCount);
      for (uint32_t j = 0; j < i; ++j) {
        assert_true(descriptor_sizes[j].type != descriptor_size.type);
      }
    }
#endif
    if (!xe_vk_pool_owner_configure(&pool_owner_, descriptor_size_count) ||
        !xe_vk_pool_owner_warm(&pool_owner_)) throw std::bad_alloc();
    if (!xe_vk_pool_search_warm(&pages_usable_)) {
      xe_vk_pool_owner_destroy(&pool_owner_);
      throw std::bad_alloc();
    }
  }
  LinkedTypeDescriptorSetAllocator(
      const LinkedTypeDescriptorSetAllocator& allocator) = delete;
  LinkedTypeDescriptorSetAllocator& operator=(
      const LinkedTypeDescriptorSetAllocator& allocator) = delete;
  ~LinkedTypeDescriptorSetAllocator() { Reset(); xe_vk_pool_search_destroy(&pages_usable_); xe_vk_pool_owner_destroy(&pool_owner_); }

  void Reset();

  VkDescriptorSet Allocate(VkDescriptorSetLayout descriptor_set_layout,
                           const VkDescriptorPoolSize* descriptor_counts,
                           uint32_t descriptor_type_count);

 private:
  const VulkanDevice* vulkan_device_;

  const VkDescriptorPoolSize* descriptor_pool_sizes_;
  uint32_t descriptor_pool_size_count_;
  uint32_t descriptor_sets_per_page_;

  // Native ownership is independent of CPU search-node reuse.
  xe_vk_pool_owner pool_owner_{};
  // Single-owner retained C89 search nodes; the latest hot page stays outside
  // the ordered index. Equal maxima retain the old multimap's insertion order.
  xe_vk_pool_search pages_usable_{};
  xe_vk_pool_search_node* page_usable_latest_ = nullptr;
};

}  // namespace vulkan
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_VULKAN_CONNECTED_DESCRIPTOR_SET_ALLOCATOR_H_
