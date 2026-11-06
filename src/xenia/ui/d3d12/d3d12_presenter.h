/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_D3D12_D3D12_PRESENTER_H_
#define XENIA_UI_D3D12_D3D12_PRESENTER_H_

#include <array>
#include <cstdint>
#include <memory>
#include <utility>

#include "xenia/base/math.h"
#include "xenia/ui/d3d12/d3d12_gpu_completion_timeline.h"
#include "xenia/ui/d3d12/d3d12_provider.h"
#include "xenia/ui/presenter.h"

namespace xe {
namespace ui {
namespace d3d12 {

class D3D12Presenter final : public Presenter {
 public:
  static constexpr DXGI_FORMAT kGuestOutputFormat =
      DXGI_FORMAT_R10G10B10A2_UNORM;
  static constexpr D3D12_RESOURCE_STATES kGuestOutputInternalState =
      D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

  // The callback must use the main direct queue of the provider.
  class D3D12GuestOutputRefreshContext final
      : public GuestOutputRefreshContext {
   public:
    D3D12GuestOutputRefreshContext(bool& is_8bpc_out_ref,
                                   ID3D12Resource* resource)
        : GuestOutputRefreshContext(is_8bpc_out_ref), resource_(resource) {}

    // kGuestOutputFormat, supports UAV. The initial state in the callback is
    // kGuestOutputInternalState, and the callback must also transition it back
    // to kGuestOutputInternalState before finishing.
    ID3D12Resource* resource_uav_capable() const { return resource_.Get(); }

   private:
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
  };

  static std::unique_ptr<D3D12Presenter> Create(
      const D3D12Provider& provider) {
    auto presenter = std::unique_ptr<D3D12Presenter>(
        new D3D12Presenter(provider));
    if (!presenter->InitializeSurfaceIndependent()) {
      return nullptr;
    }
    return presenter;
  }

  ~D3D12Presenter();

  const D3D12Provider& provider() const { return provider_; }


  bool CaptureGuestOutput(RawImage& image_out) override;

#ifdef XENIA_LIBRETRO
  // GPU blit capture: R10G10B10A2 readback with persistent resources.
  // Returns pointer to converted 8bpc buffer (valid until next call).
  bool CaptureGuestOutputGPUBlit(const void*& data_out, uint32_t& width_out,
                                 uint32_t& height_out);
#endif

  bool RefreshGuestOutputImpl(
      uint32_t mailbox_index, uint32_t frontbuffer_width,
      uint32_t frontbuffer_height,
      GuestOutputRefresher refresher, void* refresher_data,
      bool& is_8bpc_out) override;


 private:
  explicit D3D12Presenter(const D3D12Provider& provider)
      : Presenter(), provider_(provider) {}


  bool InitializeSurfaceIndependent();

  const D3D12Provider& provider_;

#ifdef XENIA_LIBRETRO
  struct GPUBlitResources {
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> command_allocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> command_list;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    UINT64 fence_value = 0;
    Microsoft::WRL::ComPtr<ID3D12Resource> readback_buffer;
    UINT64 readback_size = 0;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT readback_layout = {};
    std::vector<uint8_t> converted_pixels;
    uint32_t width = 0;
    uint32_t height = 0;
  };
  GPUBlitResources gpu_blit_;
  void DestroyGPUBlitResources();
  bool CreateGPUBlitResources(uint32_t w, uint32_t h,
                              const D3D12_RESOURCE_DESC& texture_desc);
#endif

  // The first is the refresher completion timeline submission index at which
  // the guest output texture was last refreshed, the second is the reference to
  // the texture, which may be null. The indices are the mailbox indices.
  std::array<std::pair<uint64_t, Microsoft::WRL::ComPtr<ID3D12Resource>>,
             kGuestOutputMailboxSize>
      guest_output_resources_;
  // Guest refresh submissions finish before resources are destroyed.
  std::unique_ptr<D3D12GPUCompletionTimeline>
      guest_output_resource_refresher_completion_timeline_;

};

}  // namespace d3d12
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_D3D12_D3D12_PRESENTER_H_
