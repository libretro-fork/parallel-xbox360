/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/vulkan/linked_type_descriptor_set_allocator.h"

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include <new>
#include "libretro/core_vk_pool_budget.h"

namespace xe {
namespace ui {
namespace vulkan {

void LinkedTypeDescriptorSetAllocator::Reset() {
  const VulkanDevice::Functions& dfn = vulkan_device_->functions();
  const VkDevice device = vulkan_device_->device();
  xe_vk_pool_owner_reset(&pool_owner_, device, dfn.vkDestroyDescriptorPool);
  page_usable_latest_ = nullptr;
  xe_vk_pool_search_reset(&pages_usable_);
}

VkDescriptorSet LinkedTypeDescriptorSetAllocator::Allocate(
    VkDescriptorSetLayout descriptor_set_layout,
    const VkDescriptorPoolSize* descriptor_counts,
    uint32_t descriptor_type_count) {
  assert_not_zero(descriptor_type_count);
#ifndef NDEBUG
  for (uint32_t i = 0; i < descriptor_type_count; ++i) {
    const VkDescriptorPoolSize& descriptor_count_for_type =
        descriptor_counts[i];
    assert_not_zero(descriptor_count_for_type.descriptorCount);
    for (uint32_t j = 0; j < i; ++j) {
      assert_true(descriptor_counts[j].type != descriptor_count_for_type.type);
    }
  }
#endif

  const VulkanDevice::Functions& dfn = vulkan_device_->functions();
  const VkDevice device = vulkan_device_->device();

  VkDescriptorSetAllocateInfo descriptor_set_allocate_info;
  descriptor_set_allocate_info.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  descriptor_set_allocate_info.pNext = nullptr;
  descriptor_set_allocate_info.descriptorSetCount = 1;
  descriptor_set_allocate_info.pSetLayouts = &descriptor_set_layout;
  VkDescriptorSet descriptor_set;

  xe_vk_pool_budget budget;
  if (!xe_vk_pool_budget_prepare(&budget, descriptor_pool_sizes_,
                                 descriptor_pool_size_count_, descriptor_counts,
                                 descriptor_type_count)) {
    VkDescriptorPoolCreateInfo dedicated_descriptor_pool_create_info;
    dedicated_descriptor_pool_create_info.sType =
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dedicated_descriptor_pool_create_info.pNext = nullptr;
    dedicated_descriptor_pool_create_info.flags = 0;
    dedicated_descriptor_pool_create_info.maxSets = 1;
    dedicated_descriptor_pool_create_info.poolSizeCount = descriptor_type_count;
    dedicated_descriptor_pool_create_info.pPoolSizes = descriptor_counts;
    xe_vk_pool_record* owner = xe_vk_pool_owner_acquire(&pool_owner_);
    if (!owner) throw std::bad_alloc();
    VkDescriptorPool dedicated_descriptor_pool;
    if (dfn.vkCreateDescriptorPool(
            device, &dedicated_descriptor_pool_create_info, nullptr,
            &dedicated_descriptor_pool) != VK_SUCCESS) {
      xe_vk_pool_owner_cancel(&pool_owner_);
      XELOGE(
          "LinkedTypeDescriptorSetAllocator: Failed to create a dedicated "
          "descriptor pool for a descriptor set that is too large for a pool "
          "page");
      return VK_NULL_HANDLE;
    }
    descriptor_set_allocate_info.descriptorPool = dedicated_descriptor_pool;
    if (dfn.vkAllocateDescriptorSets(device, &descriptor_set_allocate_info,
                                     &descriptor_set) != VK_SUCCESS) {
      dfn.vkDestroyDescriptorPool(device, dedicated_descriptor_pool, nullptr);
      xe_vk_pool_owner_cancel(&pool_owner_);
      XELOGE(
          "LinkedTypeDescriptorSetAllocator: Failed to allocate descriptors in "
          "a dedicated pool");
      return VK_NULL_HANDLE;
    }
    owner->pool = dedicated_descriptor_pool;
    xe_vk_pool_owner_commit(&pool_owner_);
    return descriptor_set;
  }

  // Try allocating from the latest page an allocation has happened from, to
  // avoid detaching from the map and re-attaching for every allocation.
  if (page_usable_latest_ != nullptr) {
    assert_not_zero(page_usable_latest_->descriptor_sets_remaining);
    int fit = xe_vk_pool_budget_fit(&budget,
        page_usable_latest_->descriptors_remaining,
        page_usable_latest_->descriptor_sets_remaining);
    bool latest_page_becomes_full = (fit & XE_VK_BUDGET_FULL) != 0;
    if (fit) {
      descriptor_set_allocate_info.descriptorPool = page_usable_latest_->pool;
      if (dfn.vkAllocateDescriptorSets(device, &descriptor_set_allocate_info,
                                       &descriptor_set) != VK_SUCCESS) {
        descriptor_set = VK_NULL_HANDLE;
        // Failed to allocate internally even though there should be enough
        // space, don't try to allocate from this pool again at all.
        latest_page_becomes_full = true;
      }
      if (latest_page_becomes_full) {
        xe_vk_pool_search_release(&pages_usable_, page_usable_latest_);
        page_usable_latest_ = nullptr;
      } else {
        --page_usable_latest_->descriptor_sets_remaining;
        xe_vk_pool_budget_spend(&budget, page_usable_latest_->descriptors_remaining);
      }
      if (descriptor_set != VK_NULL_HANDLE) {
        return descriptor_set;
      }
    }
  }

  uint32_t max_descriptors_per_type = xe_vk_pool_budget_request_maximum(&budget);
  // If allocating from the latest pool wasn't possible, pick any that has
  // enough free space. Prefer filling pages that have the most free space as
  // they can more likely be used for more allocations later.
  xe_vk_pool_search_node* page_usable_it_next = pages_usable_.maximum;
  while (page_usable_it_next) {
    xe_vk_pool_search_node* page_usable_it = page_usable_it_next;
    page_usable_it_next = xe_vk_pool_search_previous(page_usable_it);
    if (page_usable_it->key < max_descriptors_per_type) {
      // All other pages_usable_ entries have smaller maximum number of free
      // descriptor for any type (it's the map key).
      break;
    }
    int fit = xe_vk_pool_budget_fit(&budget,
        page_usable_it->descriptors_remaining,
        page_usable_it->descriptor_sets_remaining);
    if (!fit) continue;
    bool map_page_becomes_full = (fit & XE_VK_BUDGET_FULL) != 0;
    // Remove the page from the map unconditionally - in case of a successful
    // allocation, it will have a different number of free descriptors for
    // different types, thus potentially a new map key (but it will also become
    // page_usable_latest_ instead even), or will become full, and in case of a
    // failure to allocate internally even though there still should be enough
    // space, it should never be allocated from again.
    xe_vk_pool_search_node* map_page = page_usable_it;
    xe_vk_pool_search_erase(&pages_usable_, map_page);
    descriptor_set_allocate_info.descriptorPool = map_page->pool;
    if (dfn.vkAllocateDescriptorSets(device, &descriptor_set_allocate_info,
                                     &descriptor_set) != VK_SUCCESS) {
      descriptor_set = VK_NULL_HANDLE;
      // Failed to allocate internally even though there should be enough space,
      // don't try to allocate from this pool again at all.
      map_page_becomes_full = true;
    }
    if (map_page_becomes_full) {
      xe_vk_pool_search_release(&pages_usable_, map_page);
    } else {
      --map_page->descriptor_sets_remaining;
      xe_vk_pool_budget_spend(&budget, map_page->descriptors_remaining);
      // Move the latest page that allocation couldn't be done in to the usable
      // pages to replace it with the new one.
      if (page_usable_latest_ != nullptr) {
        // Calculate the map key (the maximum number of remaining descriptors of
        // any type).
        uint32_t latest_page_max_descriptors_remaining = xe_vk_pool_budget_maximum(
            page_usable_latest_->descriptors_remaining, descriptor_pool_size_count_);
        assert_not_zero(latest_page_max_descriptors_remaining);
        xe_vk_pool_search_insert(&pages_usable_, page_usable_latest_,
                                 latest_page_max_descriptors_remaining);
      }
      page_usable_latest_ = map_page;
    }
    if (descriptor_set != VK_NULL_HANDLE) {
      return descriptor_set;
    }
  }

  bool new_page_becomes_full = xe_vk_pool_budget_new_full(&budget, descriptor_sets_per_page_) != 0;
  // Allocate from a new page. However, if the new page becomes full
  // immediately, create a dedicated pool instead for the exact number of
  // descriptors not to leave any unused space in the pool.
  VkDescriptorPoolCreateInfo new_descriptor_pool_create_info;
  new_descriptor_pool_create_info.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  new_descriptor_pool_create_info.pNext = nullptr;
  new_descriptor_pool_create_info.flags = 0;
  if (new_page_becomes_full) {
    new_descriptor_pool_create_info.maxSets = 1;
    new_descriptor_pool_create_info.poolSizeCount = descriptor_type_count;
    new_descriptor_pool_create_info.pPoolSizes = descriptor_counts;
  } else {
    new_descriptor_pool_create_info.maxSets = descriptor_sets_per_page_;
    new_descriptor_pool_create_info.poolSizeCount = descriptor_pool_size_count_;
    new_descriptor_pool_create_info.pPoolSizes = descriptor_pool_sizes_;
  }
  // Reserve both metadata records before native creation.
  xe_vk_pool_search_node* new_page = nullptr;
  if (!new_page_becomes_full) {
    new_page = xe_vk_pool_search_acquire(&pages_usable_);
    if (!new_page) throw std::bad_alloc();
  }
  xe_vk_pool_record* owner = xe_vk_pool_owner_acquire(&pool_owner_);
  if (!owner) {
    xe_vk_pool_search_cancel(&pages_usable_);
    throw std::bad_alloc();
  }
  uint32_t* new_remaining = xe_vk_pool_owner_counts(&pool_owner_);
  VkDescriptorPool new_descriptor_pool;
  if (dfn.vkCreateDescriptorPool(device, &new_descriptor_pool_create_info,
                                 nullptr, &new_descriptor_pool) != VK_SUCCESS) {
    xe_vk_pool_owner_cancel(&pool_owner_);
    xe_vk_pool_search_cancel(&pages_usable_);
    XELOGE(
        "LinkedTypeDescriptorSetAllocator: Failed to create a descriptor pool");
    return VK_NULL_HANDLE;
  }
  descriptor_set_allocate_info.descriptorPool = new_descriptor_pool;
  if (dfn.vkAllocateDescriptorSets(device, &descriptor_set_allocate_info,
                                   &descriptor_set) != VK_SUCCESS) {
    dfn.vkDestroyDescriptorPool(device, new_descriptor_pool, nullptr);
    xe_vk_pool_owner_cancel(&pool_owner_);
    xe_vk_pool_search_cancel(&pages_usable_);
    XELOGE("LinkedTypeDescriptorSetAllocator: Failed to allocate descriptors");
    return VK_NULL_HANDLE;
  }
  owner->pool = new_descriptor_pool;
  xe_vk_pool_owner_commit(&pool_owner_);
  if (!new_page_becomes_full) {
    // Move the latest page that allocation couldn't be done in to the usable
    // pages to replace it with the new one.
    if (page_usable_latest_ != nullptr) {
      // Calculate the map key (the maximum number of remaining descriptors of
      // any type).
      uint32_t latest_page_max_descriptors_remaining = xe_vk_pool_budget_maximum(
          page_usable_latest_->descriptors_remaining, descriptor_pool_size_count_);
      assert_not_zero(latest_page_max_descriptors_remaining);
      xe_vk_pool_search_insert(&pages_usable_, page_usable_latest_,
                               latest_page_max_descriptors_remaining);
    }
    xe_vk_pool_search_commit(&pages_usable_);
    page_usable_latest_ = new_page;
    page_usable_latest_->pool = new_descriptor_pool;
    page_usable_latest_->descriptors_remaining = new_remaining;
    xe_vk_pool_budget_initialize(&budget, new_remaining);
    page_usable_latest_->descriptor_sets_remaining =
        descriptor_sets_per_page_ - 1;
  }
  return descriptor_set;
}

}  // namespace vulkan
}  // namespace ui
}  // namespace xe
