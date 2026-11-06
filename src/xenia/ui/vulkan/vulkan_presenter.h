/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_VULKAN_VULKAN_PRESENTER_H_
#define XENIA_UI_VULKAN_VULKAN_PRESENTER_H_

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "xenia/base/assert.h"
#include "xenia/ui/presenter.h"
#include "xenia/ui/vulkan/vulkan_device.h"
#include "xenia/ui/vulkan/vulkan_gpu_completion_timeline.h"
#include "xenia/ui/vulkan/vulkan_instance.h"

namespace xe {
namespace ui {
namespace vulkan {

class VulkanPresenter final : public Presenter {
 public:
  // Maximum number of different guest output image versions still potentially
  // considered alive that may be given to the refresher - this many instances
  // of dependent objects (such as framebuffers) may need to be kept by the
  // refresher across invocations (due to multiple-buffering of guest output
  // images inside the presenter, different versions may be given even every
  // invocation), to avoid recreation of dependent objects every frame.
  static constexpr size_t kMaxActiveGuestOutputImageVersions =
      kGuestOutputMailboxSize;

  static constexpr VkFormat kGuestOutputFormat =
      VK_FORMAT_A2B10G10R10_UNORM_PACK32;
  // The guest output is expected to be acquired and released in this state by
  // the refresher. The exception is the first write to the current guest output
  // image - in this case, a barrier is only needed from
  // VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT without access. Also if the image is
  // being refreshed for the first time, it's in VK_IMAGE_LAYOUT_UNDEFINED (but
  // it's safe, and preferred, to transition it from VK_IMAGE_LAYOUT_UNDEFINED
  // when writing to it in general).
  static constexpr VkPipelineStageFlagBits kGuestOutputInternalStageMask =
      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  static constexpr VkAccessFlags kGuestOutputInternalAccessMask =
      VK_ACCESS_SHADER_READ_BIT;
  static constexpr VkImageLayout kGuestOutputInternalLayout =
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

  // The callback must use the graphics and compute queue 0 of the device.
  class VulkanGuestOutputRefreshContext final
      : public GuestOutputRefreshContext {
   public:
    VulkanGuestOutputRefreshContext(bool& is_8bpc_out_ref, VkImage image,
                                    VkImageView image_view,
                                    uint64_t image_version,
                                    bool image_ever_written_previously)
        : GuestOutputRefreshContext(is_8bpc_out_ref),
          image_(image),
          image_view_(image_view),
          image_version_(image_version),
          image_ever_written_previously_(image_ever_written_previously) {}

    // The format is kGuestOutputFormat.
    // Supports usage as a color attachment and as a sampled image, as well as
    // transfer source (but the reason of that is guest output capturing).
    VkImage image() const { return image_; }
    VkImageView image_view() const { return image_view_; }
    uint64_t image_version() const { return image_version_; }
    // Whether a proper barrier must be done to acquire the image.
    bool image_ever_written_previously() const {
      return image_ever_written_previously_;
    }

   private:
    VkImage image_;
    VkImageView image_view_;
    uint64_t image_version_;
    bool image_ever_written_previously_;
  };

  static std::unique_ptr<VulkanPresenter> Create(
      VulkanDevice* vulkan_device) {
    auto presenter = std::unique_ptr<VulkanPresenter>(new VulkanPresenter(
        vulkan_device));
    return presenter;
  }

  ~VulkanPresenter();

  VulkanDevice* vulkan_device() const { return vulkan_device_; }

  bool CaptureGuestOutput(RawImage& image_out) override;

#ifdef XENIA_LIBRETRO
  // GPU blit capture: A2B10G10R10 ??? R8G8B8A8 with persistent resources.
  // Returns pointer to readback buffer (valid until next call).
  bool CaptureGuestOutputGPUBlit(const void*& data_out, uint32_t& width_out,
                                 uint32_t& height_out);

  // On a device shared with the frontend: the guest output blitted (to
  // R8G8B8A8) straight into an image of the frontend's frame, left in
  // SHADER_READ_ONLY_OPTIMAL. target_for gets the output's size and gives
  // the image, VK_NULL_HANDLE to skip. The callback/context are borrowed only
  // until return. Waits for the blit.
  bool BlitGuestOutputToImage(
      VkImage (*target_for)(void*, uint32_t, uint32_t), void* target_data,
      uint32_t& width_out, uint32_t& height_out);
#endif

  bool RefreshGuestOutputImpl(
      uint32_t mailbox_index, uint32_t frontbuffer_width,
      uint32_t frontbuffer_height,
      GuestOutputRefresher refresher, void* refresher_data,
      bool& is_8bpc_out_ref) override;


 private:
  // Usable for both the guest output image itself and for intermediate images.
  class GuestOutputImage {
   public:
    static std::unique_ptr<GuestOutputImage> Create(
        const VulkanDevice* const vulkan_device, const uint32_t width,
        const uint32_t height) {
      assert_not_zero(width);
      assert_not_zero(height);
      auto image = std::unique_ptr<GuestOutputImage>(
          new GuestOutputImage(vulkan_device, width, height));
      if (!image->Initialize()) {
        return nullptr;
      }
      return std::move(image);
    }

    GuestOutputImage(const GuestOutputImage& image) = delete;
    GuestOutputImage& operator=(const GuestOutputImage& image) = delete;
    ~GuestOutputImage();

    const VkExtent2D& extent() const { return extent_; }

    VkImage image() const { return image_; }
    VkDeviceMemory memory() const { return memory_; }
    VkImageView view() const { return view_; }

   private:
    GuestOutputImage(const VulkanDevice* const vulkan_device,
                     const uint32_t width, const uint32_t height)
        : vulkan_device_(vulkan_device) {
      extent_.width = width;
      extent_.height = height;
    }

    bool Initialize();

    const VulkanDevice* vulkan_device_;

    VkExtent2D extent_;
    VkImage image_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkImageView view_ = VK_NULL_HANDLE;
  };

  struct GuestOutputImageInstance {
    // Refresher-side reference (painting has its own references for the purpose
    // of destruction only after painting is done on the GPU).
    std::shared_ptr<GuestOutputImage> image;
    uint64_t version = UINT64_MAX;
    uint64_t last_refresher_submission = 0;
    // For choosing the barrier stage and access mask and layout depending on
    // whether the image has previously been written. If an image is active
    // after a refresh, it can be assumed that this is true.
    bool ever_successfully_refreshed = false;

    void SetToNewImage(const std::shared_ptr<GuestOutputImage>& new_image,
                       uint64_t new_version) {
      image = new_image;
      version = new_version;
      last_refresher_submission = 0;
      ever_successfully_refreshed = false;
    }
  };

  explicit VulkanPresenter(VulkanDevice* device)
      : Presenter(), vulkan_device_(device),
        guest_output_image_refresher_completion_timeline_(device, "guest-refresher") {
    assert_not_null(device);
  }
  VulkanDevice* vulkan_device_;
#ifdef XENIA_LIBRETRO
  struct GPUBlitResources {
    VkImage blit_image = VK_NULL_HANDLE;
    VkDeviceMemory blit_memory = VK_NULL_HANDLE;
    VkBuffer readback_buffer = VK_NULL_HANDLE;
    VkDeviceMemory readback_memory = VK_NULL_HANDLE;
    void* readback_mapped = nullptr;
    VkCommandPool cmd_pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    uint32_t width = 0;
    uint32_t height = 0;
    // BlitGuestOutputToImage's, made on first use
    VkCommandPool direct_cmd_pool = VK_NULL_HANDLE;
    VkCommandBuffer direct_cmd = VK_NULL_HANDLE;
  };
  GPUBlitResources gpu_blit_;
  void DestroyGPUBlitResources();
  bool CreateGPUBlitResources(uint32_t w, uint32_t h);
#endif

  // Value monotonically increased every time a new guest output image is
  // initialized, for recreation of dependent objects such as framebuffers in
  // the refreshers - saving and comparing the handle in the refresher is not
  // enough as Create > Destroy > Create may result in the same handle for
  // actually different objects without the refresher being aware of the
  // destruction.
  uint64_t guest_output_image_next_version_ = 0;
  std::array<GuestOutputImageInstance, kGuestOutputMailboxSize>
      guest_output_images_;
  VulkanGPUCompletionTimeline guest_output_image_refresher_completion_timeline_;

};

}  // namespace vulkan
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_VULKAN_VULKAN_PRESENTER_H_
