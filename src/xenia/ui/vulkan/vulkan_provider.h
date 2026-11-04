/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_VULKAN_VULKAN_PROVIDER_H_
#define XENIA_UI_VULKAN_VULKAN_PROVIDER_H_

#include <memory>

#include "xenia/ui/graphics_provider.h"
#include "xenia/ui/vulkan/vulkan_device.h"
#include "xenia/ui/vulkan/vulkan_instance.h"

namespace xe {
namespace ui {
namespace vulkan {

class VulkanProvider : public GraphicsProvider {
 public:
  static std::unique_ptr<VulkanProvider> Create(bool with_gpu_emulation,
                                                bool with_presentation);

#ifdef XENIA_LIBRETRO
  // The frontend's instance and the device the core made on it (RetroArch's
  // context negotiation), kept by the core for as long as the frontend keeps
  // the device: Create takes them instead of making its own. nullptr for
  // none.
  static void SetExternal(VulkanInstance* instance, VulkanDevice* device);

  VulkanInstance* vulkan_instance() const {
    return external_instance_ ? external_instance_ : vulkan_instance_.get();
  }
  VulkanDevice* vulkan_device() const {
    return external_device_ ? external_device_ : vulkan_device_.get();
  }
#else
  VulkanInstance* vulkan_instance() const { return vulkan_instance_.get(); }
  VulkanDevice* vulkan_device() const { return vulkan_device_.get(); }
#endif

  // nullptr if created without presentation support.

  std::unique_ptr<Presenter> CreatePresenter(
      Presenter::HostGpuLossCallback host_gpu_loss_callback =
          Presenter::FatalErrorHostGpuLossCallback) override;


 private:
  explicit VulkanProvider() = default;

  std::unique_ptr<VulkanInstance> vulkan_instance_;

  // Depends on the instance.
  std::unique_ptr<VulkanDevice> vulkan_device_;

  // Depends on the device.
#ifdef XENIA_LIBRETRO
  VulkanInstance* external_instance_ = nullptr;
  VulkanDevice* external_device_ = nullptr;
#endif
};

}  // namespace vulkan
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_VULKAN_VULKAN_PROVIDER_H_
