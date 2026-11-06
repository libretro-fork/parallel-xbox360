/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/vulkan/vulkan_presenter.h"

#include <cstdint>

#include "xenia/base/assert.h"
#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/platform.h"
#include "xenia/ui/vulkan/vulkan_util.h"

namespace xe {
namespace ui {
namespace vulkan {

VulkanPresenter::~VulkanPresenter() {
  guest_output_image_refresher_completion_timeline_.AwaitAllSubmissions();
  DestroyGPUBlitResources();
}
#ifdef XENIA_LIBRETRO
void VulkanPresenter::DestroyGPUBlitResources() {
  const VulkanDevice::Functions& dfn = vulkan_device_->functions();
  const VkDevice device = vulkan_device_->device();

  // No wait here: every blit is submitted through a completion timeline and
  // awaited before CaptureGuestOutputGPUBlit returns. gpu_blit_.fence is never
  // submitted, so waiting on it (unsignaled, no timeout) hung every Vulkan
  // unload forever (NNshi).
  if (gpu_blit_.readback_mapped && gpu_blit_.readback_memory) {
    dfn.vkUnmapMemory(device, gpu_blit_.readback_memory);
  }
  if (gpu_blit_.readback_buffer) dfn.vkDestroyBuffer(device, gpu_blit_.readback_buffer, nullptr);
  if (gpu_blit_.readback_memory) dfn.vkFreeMemory(device, gpu_blit_.readback_memory, nullptr);
  if (gpu_blit_.blit_image) dfn.vkDestroyImage(device, gpu_blit_.blit_image, nullptr);
  if (gpu_blit_.blit_memory) dfn.vkFreeMemory(device, gpu_blit_.blit_memory, nullptr);
  if (gpu_blit_.fence) dfn.vkDestroyFence(device, gpu_blit_.fence, nullptr);
  if (gpu_blit_.cmd_pool) dfn.vkDestroyCommandPool(device, gpu_blit_.cmd_pool, nullptr);
  if (gpu_blit_.direct_cmd_pool) dfn.vkDestroyCommandPool(device, gpu_blit_.direct_cmd_pool, nullptr);
  gpu_blit_ = {};
}

bool VulkanPresenter::CreateGPUBlitResources(uint32_t w, uint32_t h) {
  const VulkanDevice::Functions& dfn = vulkan_device_->functions();
  const VkDevice device = vulkan_device_->device();
  uint32_t qf = vulkan_device_->queue_family_graphics_compute();

  // Command pool
  VkCommandPoolCreateInfo pool_info = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
  pool_info.queueFamilyIndex = qf;
  if (dfn.vkCreateCommandPool(device, &pool_info, nullptr, &gpu_blit_.cmd_pool) != VK_SUCCESS) {
    XELOGE("VulkanPresenter: Failed to create GPU blit command pool");
    return false;
  }

  VkCommandBufferAllocateInfo alloc_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  alloc_info.commandPool = gpu_blit_.cmd_pool;
  alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  alloc_info.commandBufferCount = 1;
  if (dfn.vkAllocateCommandBuffers(device, &alloc_info, &gpu_blit_.cmd) != VK_SUCCESS) {
    XELOGE("VulkanPresenter: Failed to allocate GPU blit command buffer");
    return false;
  }

  // Fence
  VkFenceCreateInfo fence_info = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  if (dfn.vkCreateFence(device, &fence_info, nullptr, &gpu_blit_.fence) != VK_SUCCESS) {
    XELOGE("VulkanPresenter: Failed to create GPU blit fence");
    return false;
  }

  // Intermediate R8G8B8A8 blit image (device-local)
  VkImageCreateInfo img_info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  img_info.imageType = VK_IMAGE_TYPE_2D;
  img_info.format = VK_FORMAT_R8G8B8A8_UNORM;
  img_info.extent = {w, h, 1};
  img_info.mipLevels = 1;
  img_info.arrayLayers = 1;
  img_info.samples = VK_SAMPLE_COUNT_1_BIT;
  img_info.tiling = VK_IMAGE_TILING_OPTIMAL;
  img_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  img_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  img_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (!util::CreateDedicatedAllocationImage(
          vulkan_device_, img_info, util::MemoryPurpose::kDeviceLocal,
          gpu_blit_.blit_image, gpu_blit_.blit_memory)) {
    XELOGE("VulkanPresenter: Failed to create GPU blit image");
    return false;
  }

  // Readback buffer
  VkDeviceSize buf_size = VkDeviceSize(w) * h * 4;
  if (!util::CreateDedicatedAllocationBuffer(
          vulkan_device_, buf_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
          util::MemoryPurpose::kReadback, gpu_blit_.readback_buffer,
          gpu_blit_.readback_memory)) {
    XELOGE("VulkanPresenter: Failed to create GPU blit readback buffer");
    return false;
  }
  if (dfn.vkMapMemory(device, gpu_blit_.readback_memory, 0, VK_WHOLE_SIZE, 0,
                       &gpu_blit_.readback_mapped) != VK_SUCCESS) {
    XELOGE("VulkanPresenter: Failed to map GPU blit readback buffer");
    return false;
  }

  gpu_blit_.width = w;
  gpu_blit_.height = h;
  return true;
}

bool VulkanPresenter::CaptureGuestOutputGPUBlit(const void*& data_out,
                                                 uint32_t& width_out,
                                                 uint32_t& height_out) {
  // Acquire the guest output image
  std::shared_ptr<GuestOutputImage> guest_output_image;
  {
    uint32_t guest_output_mailbox_index;
    auto guest_output_consumer_lease(
        ConsumeGuestOutput(guest_output_mailbox_index));
    if (guest_output_mailbox_index != UINT32_MAX) {
      assert_true(guest_output_images_[guest_output_mailbox_index]
                      .ever_successfully_refreshed);
      guest_output_image =
          guest_output_images_[guest_output_mailbox_index].image;
    }
  }
  if (!guest_output_image) {
    return false;
  }

  VkExtent2D extent = guest_output_image->extent();
  uint32_t w = extent.width;
  uint32_t h = extent.height;
  if (w == 0 || h == 0) return false;

  // Ensure persistent resources match the current dimensions
  if (gpu_blit_.width != w || gpu_blit_.height != h) {
    DestroyGPUBlitResources();
    if (!CreateGPUBlitResources(w, h)) {
      DestroyGPUBlitResources();
      return false;
    }
  }

  const VulkanDevice::Functions& dfn = vulkan_device_->functions();
  const VkDevice device = vulkan_device_->device();

  // Reset command pool (implicitly resets the command buffer)
  dfn.vkResetCommandPool(device, gpu_blit_.cmd_pool, 0);

  VkCommandBufferBeginInfo begin_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  if (dfn.vkBeginCommandBuffer(gpu_blit_.cmd, &begin_info) != VK_SUCCESS) {
    return false;
  }

  VkImageMemoryBarrier barriers[2] = {};

  // Barrier: guest output SHADER_READ ??? TRANSFER_SRC
  barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barriers[0].srcAccessMask = kGuestOutputInternalAccessMask;
  barriers[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  barriers[0].oldLayout = kGuestOutputInternalLayout;
  barriers[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barriers[0].image = guest_output_image->image();
  barriers[0].subresourceRange = util::InitializeSubresourceRange();

  // Barrier: blit_image UNDEFINED ??? TRANSFER_DST
  barriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barriers[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  barriers[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  barriers[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barriers[1].image = gpu_blit_.blit_image;
  barriers[1].subresourceRange = util::InitializeSubresourceRange();

  dfn.vkCmdPipelineBarrier(gpu_blit_.cmd,
      kGuestOutputInternalStageMask | VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      0, 0, nullptr, 0, nullptr, 2, barriers);

  // vkCmdBlitImage: A2B10G10R10 ??? R8G8B8A8 with GPU format conversion
  VkImageBlit blit_region = {};
  blit_region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  blit_region.srcSubresource.layerCount = 1;
  blit_region.srcOffsets[1] = {int32_t(w), int32_t(h), 1};
  blit_region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  blit_region.dstSubresource.layerCount = 1;
  blit_region.dstOffsets[1] = {int32_t(w), int32_t(h), 1};
  dfn.vkCmdBlitImage(gpu_blit_.cmd,
      guest_output_image->image(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      gpu_blit_.blit_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      1, &blit_region, VK_FILTER_NEAREST);

  // Barrier: restore guest output, transition blit_image to TRANSFER_SRC
  barriers[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  barriers[0].dstAccessMask = kGuestOutputInternalAccessMask;
  barriers[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  barriers[0].newLayout = kGuestOutputInternalLayout;
  barriers[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  barriers[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  barriers[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barriers[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

  dfn.vkCmdPipelineBarrier(gpu_blit_.cmd,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT | kGuestOutputInternalStageMask,
      0, 0, nullptr, 0, nullptr, 2, barriers);

  // Copy blit_image ??? readback_buffer
  VkBufferImageCopy copy_region = {};
  copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  copy_region.imageSubresource.layerCount = 1;
  copy_region.imageExtent = {w, h, 1};
  dfn.vkCmdCopyImageToBuffer(gpu_blit_.cmd, gpu_blit_.blit_image,
      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      gpu_blit_.readback_buffer, 1, &copy_region);

  // Host visibility barrier
  VkBufferMemoryBarrier buf_barrier = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
  buf_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  buf_barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
  buf_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  buf_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  buf_barrier.buffer = gpu_blit_.readback_buffer;
  buf_barrier.size = VK_WHOLE_SIZE;
  dfn.vkCmdPipelineBarrier(gpu_blit_.cmd,
      VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
      0, 0, nullptr, 1, &buf_barrier, 0, nullptr);

  if (dfn.vkEndCommandBuffer(gpu_blit_.cmd) != VK_SUCCESS) {
    return false;
  }

  // Submit and wait
  {
    VulkanGPUCompletionTimeline completion_timeline(vulkan_device_);
    VkSubmitInfo submit_info = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &gpu_blit_.cmd;
    const VkResult submit_result = completion_timeline.AcquireFenceAndSubmit(
        vulkan_device_->queue_family_graphics_compute(), 0, 1, &submit_info);
    if (submit_result != VK_SUCCESS) {
      XELOGE("VulkanPresenter: Failed to submit GPU blit command buffer: {}",
             vk::to_string(vk::Result(submit_result)));
      return false;
    }
    // Destroying the completion timeline causes the submission to be awaited.
  }

  // Return mapped readback pointer
  data_out = gpu_blit_.readback_mapped;
  width_out = w;
  height_out = h;
  return true;
}
#endif  // XENIA_LIBRETRO

#ifdef XENIA_LIBRETRO
bool VulkanPresenter::BlitGuestOutputToImage(
    VkImage (*target_for)(void*, uint32_t, uint32_t), void* target_data,
    uint32_t& width_out, uint32_t& height_out) {
  std::shared_ptr<GuestOutputImage> guest_output_image;
  {
    uint32_t guest_output_mailbox_index;
    auto guest_output_consumer_lease(
        ConsumeGuestOutput(guest_output_mailbox_index));
    if (guest_output_mailbox_index != UINT32_MAX) {
      guest_output_image =
          guest_output_images_[guest_output_mailbox_index].image;
    }
  }
  if (!guest_output_image) {
    return false;
  }
  const VkExtent2D extent = guest_output_image->extent();
  const uint32_t w = extent.width, h = extent.height;
  if (!w || !h) {
    return false;
  }
  const VkImage target = target_for(target_data, w, h);
  if (target == VK_NULL_HANDLE) {
    return false;
  }

  const VulkanDevice::Functions& dfn = vulkan_device_->functions();
  const VkDevice device = vulkan_device_->device();
  if (gpu_blit_.direct_cmd_pool == VK_NULL_HANDLE) {
    VkCommandPoolCreateInfo pool_info = {
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    pool_info.queueFamilyIndex =
        vulkan_device_->queue_family_graphics_compute();
    if (dfn.vkCreateCommandPool(device, &pool_info, nullptr,
                                &gpu_blit_.direct_cmd_pool) != VK_SUCCESS) {
      gpu_blit_.direct_cmd_pool = VK_NULL_HANDLE;
      return false;
    }
    VkCommandBufferAllocateInfo alloc_info = {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc_info.commandPool = gpu_blit_.direct_cmd_pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1;
    if (dfn.vkAllocateCommandBuffers(device, &alloc_info,
                                     &gpu_blit_.direct_cmd) != VK_SUCCESS) {
      return false;
    }
  }
  const VkCommandBuffer cmd = gpu_blit_.direct_cmd;
  dfn.vkResetCommandPool(device, gpu_blit_.direct_cmd_pool, 0);
  VkCommandBufferBeginInfo begin_info = {
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  if (dfn.vkBeginCommandBuffer(cmd, &begin_info) != VK_SUCCESS) {
    return false;
  }

  VkImageMemoryBarrier barriers[2] = {};
  barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barriers[0].srcAccessMask = kGuestOutputInternalAccessMask;
  barriers[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  barriers[0].oldLayout = kGuestOutputInternalLayout;
  barriers[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barriers[0].image = guest_output_image->image();
  barriers[0].subresourceRange = util::InitializeSubresourceRange();
  // The frontend's previous read of the image is done: it waits on its
  // frame's sync index before the core is given the image again
  barriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barriers[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  barriers[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  barriers[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barriers[1].image = target;
  barriers[1].subresourceRange = util::InitializeSubresourceRange();
  dfn.vkCmdPipelineBarrier(
      cmd, kGuestOutputInternalStageMask | VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 2, barriers);

  VkImageBlit blit_region = {};
  blit_region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  blit_region.srcSubresource.layerCount = 1;
  blit_region.srcOffsets[1] = {int32_t(w), int32_t(h), 1};
  blit_region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  blit_region.dstSubresource.layerCount = 1;
  blit_region.dstOffsets[1] = {int32_t(w), int32_t(h), 1};
  dfn.vkCmdBlitImage(cmd, guest_output_image->image(),
                     VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, target,
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit_region,
                     VK_FILTER_NEAREST);

  barriers[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  barriers[0].dstAccessMask = kGuestOutputInternalAccessMask;
  barriers[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  barriers[0].newLayout = kGuestOutputInternalLayout;
  barriers[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  barriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  barriers[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barriers[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  dfn.vkCmdPipelineBarrier(
      cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
      kGuestOutputInternalStageMask | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
      0, 0, nullptr, 0, nullptr, 2, barriers);

  if (dfn.vkEndCommandBuffer(cmd) != VK_SUCCESS) {
    return false;
  }
  {
    // Awaited when the timeline goes: the frontend draws the image in this
    // same retro_run, on its own queue
    VulkanGPUCompletionTimeline completion_timeline(vulkan_device_);
    VkSubmitInfo submit_info = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &cmd;
    const VkResult submit_result = completion_timeline.AcquireFenceAndSubmit(
        vulkan_device_->queue_family_graphics_compute(), 0, 1, &submit_info);
    if (submit_result != VK_SUCCESS) {
      XELOGE("VulkanPresenter: Failed to submit the guest output blit: {}",
             vk::to_string(vk::Result(submit_result)));
      return false;
    }
  }
  width_out = w;
  height_out = h;
  return true;
}
#endif

bool VulkanPresenter::CaptureGuestOutput(RawImage& image_out) {
  std::shared_ptr<GuestOutputImage> guest_output_image;
  {
    uint32_t guest_output_mailbox_index;
    auto guest_output_consumer_lease(
        ConsumeGuestOutput(guest_output_mailbox_index));
    if (guest_output_mailbox_index != UINT32_MAX) {
      assert_true(guest_output_images_[guest_output_mailbox_index]
                      .ever_successfully_refreshed);
      guest_output_image =
          guest_output_images_[guest_output_mailbox_index].image;
    }
    // Incremented the reference count of the guest output image - safe to leave
    // the consumer critical section now.
  }
  if (!guest_output_image) {
    return false;
  }

  VkExtent2D image_extent = guest_output_image->extent();
  size_t pixel_count = size_t(image_extent.width) * image_extent.height;
  VkDeviceSize buffer_size = VkDeviceSize(sizeof(uint32_t) * pixel_count);
  VkBuffer buffer;
  VkDeviceMemory buffer_memory;
  if (!util::CreateDedicatedAllocationBuffer(
          vulkan_device_, buffer_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
          util::MemoryPurpose::kReadback, buffer, buffer_memory)) {
    XELOGE("VulkanPresenter: Failed to create the guest output capture buffer");
    return false;
  }

  const VulkanDevice::Functions& dfn = vulkan_device_->functions();
  const VkDevice device = vulkan_device_->device();

  {
    VkCommandPoolCreateInfo command_pool_create_info;
    command_pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    command_pool_create_info.pNext = nullptr;
    command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    command_pool_create_info.queueFamilyIndex =
        vulkan_device_->queue_family_graphics_compute();
    VkCommandPool command_pool;
    if (dfn.vkCreateCommandPool(device, &command_pool_create_info, nullptr,
                                &command_pool) != VK_SUCCESS) {
      XELOGE(
          "VulkanPresenter: Failed to create the guest output capturing "
          "command pool");
      dfn.vkDestroyBuffer(device, buffer, nullptr);
      dfn.vkFreeMemory(device, buffer_memory, nullptr);
      return false;
    }

    VkCommandBufferAllocateInfo command_buffer_allocate_info;
    command_buffer_allocate_info.sType =
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    command_buffer_allocate_info.pNext = nullptr;
    command_buffer_allocate_info.commandPool = command_pool;
    command_buffer_allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_buffer_allocate_info.commandBufferCount = 1;
    VkCommandBuffer command_buffer;
    if (dfn.vkAllocateCommandBuffers(device, &command_buffer_allocate_info,
                                     &command_buffer) != VK_SUCCESS) {
      XELOGE(
          "VulkanPresenter: Failed to allocate the guest output capturing "
          "command buffer");
      dfn.vkDestroyCommandPool(device, command_pool, nullptr);
      dfn.vkDestroyBuffer(device, buffer, nullptr);
      dfn.vkFreeMemory(device, buffer_memory, nullptr);
      return false;
    }

    VkCommandBufferBeginInfo command_buffer_begin_info;
    command_buffer_begin_info.sType =
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    command_buffer_begin_info.pNext = nullptr;
    command_buffer_begin_info.flags =
        VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    command_buffer_begin_info.pInheritanceInfo = nullptr;
    if (dfn.vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info) !=
        VK_SUCCESS) {
      XELOGE(
          "VulkanPresenter: Failed to begin recording the guest output "
          "capturing command buffer");
      dfn.vkDestroyCommandPool(device, command_pool, nullptr);
      dfn.vkDestroyBuffer(device, buffer, nullptr);
      dfn.vkFreeMemory(device, buffer_memory, nullptr);
      return false;
    }

    VkImageMemoryBarrier image_memory_barrier;
    image_memory_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    image_memory_barrier.pNext = nullptr;
    image_memory_barrier.srcAccessMask = kGuestOutputInternalAccessMask;
    image_memory_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    image_memory_barrier.oldLayout = kGuestOutputInternalLayout;
    image_memory_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    image_memory_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    image_memory_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    image_memory_barrier.image = guest_output_image->image();
    image_memory_barrier.subresourceRange = util::InitializeSubresourceRange();
    dfn.vkCmdPipelineBarrier(command_buffer, kGuestOutputInternalStageMask,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                             nullptr, 1, &image_memory_barrier);

    VkBufferImageCopy buffer_image_copy = {};
    buffer_image_copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    buffer_image_copy.imageSubresource.layerCount = 1;
    buffer_image_copy.imageExtent.width = image_extent.width;
    buffer_image_copy.imageExtent.height = image_extent.height;
    buffer_image_copy.imageExtent.depth = 1;
    dfn.vkCmdCopyImageToBuffer(command_buffer, guest_output_image->image(),
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1,
                               &buffer_image_copy);

    // A fence doesn't guarantee host visibility and availability.
    VkBufferMemoryBarrier buffer_memory_barrier;
    buffer_memory_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    buffer_memory_barrier.pNext = nullptr;
    buffer_memory_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    buffer_memory_barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    buffer_memory_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    buffer_memory_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    buffer_memory_barrier.buffer = buffer;
    buffer_memory_barrier.offset = 0;
    buffer_memory_barrier.size = VK_WHOLE_SIZE;
    std::swap(image_memory_barrier.srcAccessMask,
              image_memory_barrier.dstAccessMask);
    std::swap(image_memory_barrier.oldLayout, image_memory_barrier.newLayout);
    dfn.vkCmdPipelineBarrier(
        command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT | kGuestOutputInternalStageMask, 0, 0,
        nullptr, 1, &buffer_memory_barrier, 1, &image_memory_barrier);

    if (dfn.vkEndCommandBuffer(command_buffer) != VK_SUCCESS) {
      XELOGE(
          "VulkanPresenter: Failed to end recording the guest output capturing "
          "command buffer");
      dfn.vkDestroyCommandPool(device, command_pool, nullptr);
      dfn.vkDestroyBuffer(device, buffer, nullptr);
      dfn.vkFreeMemory(device, buffer_memory, nullptr);
      return false;
    }

    {
      VulkanGPUCompletionTimeline completion_timeline(vulkan_device_,
                                                      "guest-capture");
      VkSubmitInfo submit_info = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
      submit_info.commandBufferCount = 1;
      submit_info.pCommandBuffers = &command_buffer;
      const VkResult submit_result = completion_timeline.AcquireFenceAndSubmit(
          vulkan_device_->queue_family_graphics_compute(), 0, 1, &submit_info);
      if (submit_result != VK_SUCCESS) {
        XELOGE(
            "VulkanPresenter: Failed to submit the guest output capturing "
            "command buffer: {}",
            vk::to_string(vk::Result(submit_result)));
        dfn.vkDestroyCommandPool(device, command_pool, nullptr);
        dfn.vkDestroyBuffer(device, buffer, nullptr);
        dfn.vkFreeMemory(device, buffer_memory, nullptr);
        return false;
      }
      // Destroying the completion timeline causes the submission to be awaited.
    }

    dfn.vkDestroyCommandPool(device, command_pool, nullptr);
  }

  // Don't need the buffer anymore, just its memory.
  dfn.vkDestroyBuffer(device, buffer, nullptr);

  void* mapping;
  if (dfn.vkMapMemory(device, buffer_memory, 0, VK_WHOLE_SIZE, 0, &mapping) !=
      VK_SUCCESS) {
    XELOGE("VulkanPresenter: Failed to map the guest output capture memory");
    dfn.vkFreeMemory(device, buffer_memory, nullptr);
    return false;
  }

  image_out.width = image_extent.width;
  image_out.height = image_extent.height;
  image_out.stride = sizeof(uint32_t) * image_extent.width;
  image_out.data.resize(size_t(buffer_size));
  uint32_t* image_out_pixels =
      reinterpret_cast<uint32_t*>(image_out.data.data());
  for (size_t i = 0; i < pixel_count; ++i) {
    image_out_pixels[i] = Packed10bpcRGBTo8bpcBytes(
        reinterpret_cast<const uint32_t*>(mapping)[i]);
  }

  // Unmapping will be done by freeing.
  dfn.vkFreeMemory(device, buffer_memory, nullptr);

  return true;
}

bool VulkanPresenter::RefreshGuestOutputImpl(
    uint32_t mailbox_index, uint32_t frontbuffer_width,
    uint32_t frontbuffer_height,
    GuestOutputRefresher refresher, void* refresher_data,
    bool& is_8bpc_out_ref) {
  assert_not_zero(frontbuffer_width);
  assert_not_zero(frontbuffer_height);
  VkExtent2D max_framebuffer_extent =
      util::GetMax2DFramebufferExtent(vulkan_device_->properties());
  if (frontbuffer_width > max_framebuffer_extent.width ||
      frontbuffer_height > max_framebuffer_extent.height) {
    // Writing the guest output isn't supposed to rescale, and a guest texture
    // exceeding the maximum size won't be loadable anyway.
    return false;
  }

  GuestOutputImageInstance& image_instance =
      guest_output_images_[mailbox_index];
  if (image_instance.image &&
      (image_instance.image->extent().width != frontbuffer_width ||
       image_instance.image->extent().height != frontbuffer_height)) {
    guest_output_image_refresher_completion_timeline_
        .AwaitSubmissionAndUpdateCompleted(
            image_instance.last_refresher_submission);
    image_instance.image.reset();
  }
  if (!image_instance.image) {
    std::unique_ptr<GuestOutputImage> new_image = GuestOutputImage::Create(
        vulkan_device_, frontbuffer_width, frontbuffer_height);
    if (!new_image) {
      return false;
    }
    image_instance.SetToNewImage(std::move(new_image),
                                 guest_output_image_next_version_++);
  }

  VulkanGuestOutputRefreshContext context(
      is_8bpc_out_ref, image_instance.image->image(),
      image_instance.image->view(), image_instance.version,
      image_instance.ever_successfully_refreshed);
  bool refresher_succeeded = refresher(refresher_data, context);
  if (refresher_succeeded) {
    image_instance.ever_successfully_refreshed = true;
  }
  // Even if the refresher has returned false, it still might have submitted
  // some commands referencing the image. It's better to put an excessive
  // signal and wait slightly longer, for nothing important, while shutting down
  // than to destroy the image while it's still in use.
  image_instance.last_refresher_submission =
      guest_output_image_refresher_completion_timeline_.GetUpcomingSubmission();
  // No need to make the refresher signal the fence by itself - signal it here
  // instead to have more control:
  // "Fence signal operations that are defined by vkQueueSubmit additionally
  //  include in the first synchronization scope all commands that occur earlier
  //  in submission order."
  const VkResult submit_result =
      guest_output_image_refresher_completion_timeline_.AcquireFenceAndSubmit(
          vulkan_device_->queue_family_graphics_compute(), 0, 0, nullptr);
  if (submit_result != VK_SUCCESS) {
    XELOGE(
        "VulkanPresenter: Failed to submit the guest output image refresh "
        "fence signal: {}",
        vk::to_string(vk::Result(submit_result)));
  }
  return refresher_succeeded;
}

VulkanPresenter::GuestOutputImage::~GuestOutputImage() {
  const VulkanDevice::Functions& dfn = vulkan_device_->functions();
  const VkDevice device = vulkan_device_->device();
  if (view_ != VK_NULL_HANDLE) {
    dfn.vkDestroyImageView(device, view_, nullptr);
  }
  if (image_ != VK_NULL_HANDLE) {
    dfn.vkDestroyImage(device, image_, nullptr);
  }
  if (memory_ != VK_NULL_HANDLE) {
    dfn.vkFreeMemory(device, memory_, nullptr);
  }
}

bool VulkanPresenter::GuestOutputImage::Initialize() {
  VkImageCreateInfo image_create_info;
  image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  image_create_info.pNext = nullptr;
  image_create_info.flags = 0;
  image_create_info.imageType = VK_IMAGE_TYPE_2D;
  image_create_info.format = kGuestOutputFormat;
  image_create_info.extent.width = extent_.width;
  image_create_info.extent.height = extent_.height;
  image_create_info.extent.depth = 1;
  image_create_info.mipLevels = 1;
  image_create_info.arrayLayers = 1;
  image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
  image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
  image_create_info.usage =
      VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
  image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  image_create_info.queueFamilyIndexCount = 0;
  image_create_info.pQueueFamilyIndices = nullptr;
  image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (!ui::vulkan::util::CreateDedicatedAllocationImage(
          vulkan_device_, image_create_info,
          ui::vulkan::util::MemoryPurpose::kDeviceLocal, image_, memory_)) {
    XELOGE("VulkanPresenter: Failed to create a guest output image");
    return false;
  }

  const VulkanDevice::Functions& dfn = vulkan_device_->functions();
  const VkDevice device = vulkan_device_->device();

  VkImageViewCreateInfo image_view_create_info;
  image_view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  image_view_create_info.pNext = nullptr;
  image_view_create_info.flags = 0;
  image_view_create_info.image = image_;
  image_view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
  image_view_create_info.format = kGuestOutputFormat;
  image_view_create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
  image_view_create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
  image_view_create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
  image_view_create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
  image_view_create_info.subresourceRange.aspectMask =
      VK_IMAGE_ASPECT_COLOR_BIT;
  image_view_create_info.subresourceRange.baseMipLevel = 0;
  image_view_create_info.subresourceRange.levelCount = 1;
  image_view_create_info.subresourceRange.baseArrayLayer = 0;
  image_view_create_info.subresourceRange.layerCount = 1;
  if (dfn.vkCreateImageView(device, &image_view_create_info, nullptr, &view_) !=
      VK_SUCCESS) {
    XELOGE("VulkanPresenter: Failed to create a guest output image view");
    return false;
  }

  return true;
}

}  // namespace vulkan
}  // namespace ui
}  // namespace xe
