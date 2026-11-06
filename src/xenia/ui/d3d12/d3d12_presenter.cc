/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/d3d12/d3d12_presenter.h"

#include <climits>
#include <cstdint>
#include <memory>
#include <utility>

#include "xenia/base/assert.h"
#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/ui/d3d12/d3d12_provider.h"
#include "xenia/ui/d3d12/d3d12_util.h"
namespace xe {
namespace ui {
namespace d3d12 {

D3D12Presenter::~D3D12Presenter() {
  if (guest_output_resource_refresher_completion_timeline_) {
    guest_output_resource_refresher_completion_timeline_->AwaitAllSubmissions();
  }

#ifdef XENIA_LIBRETRO
  DestroyGPUBlitResources();
#endif
}

#ifdef XENIA_LIBRETRO
void D3D12Presenter::DestroyGPUBlitResources() {
  // Wait for any in-flight work
  if (gpu_blit_.fence && gpu_blit_.fence_value > 0) {
    if (gpu_blit_.fence->GetCompletedValue() < gpu_blit_.fence_value) {
      gpu_blit_.fence->SetEventOnCompletion(gpu_blit_.fence_value, nullptr);
    }
  }
  gpu_blit_.command_list.Reset();
  gpu_blit_.command_allocator.Reset();
  gpu_blit_.fence.Reset();
  gpu_blit_.readback_buffer.Reset();
  gpu_blit_.fence_value = 0;
  gpu_blit_.readback_size = 0;
  gpu_blit_.readback_layout = {};
  gpu_blit_.converted_pixels.clear();
  gpu_blit_.width = 0;
  gpu_blit_.height = 0;
}

bool D3D12Presenter::CreateGPUBlitResources(
    uint32_t w, uint32_t h, const D3D12_RESOURCE_DESC& texture_desc) {
  ID3D12Device* device = provider_.GetDevice();

  // Command allocator
  if (FAILED(device->CreateCommandAllocator(
          D3D12_COMMAND_LIST_TYPE_DIRECT,
          IID_PPV_ARGS(&gpu_blit_.command_allocator)))) {
    XELOGE("D3D12Presenter: Failed to create GPU blit command allocator");
    return false;
  }

  // Command list
  if (FAILED(device->CreateCommandList(
          0, D3D12_COMMAND_LIST_TYPE_DIRECT,
          gpu_blit_.command_allocator.Get(), nullptr,
          IID_PPV_ARGS(&gpu_blit_.command_list)))) {
    XELOGE("D3D12Presenter: Failed to create GPU blit command list");
    return false;
  }
  gpu_blit_.command_list->Close();

  // Fence
  if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                  IID_PPV_ARGS(&gpu_blit_.fence)))) {
    XELOGE("D3D12Presenter: Failed to create GPU blit fence");
    return false;
  }

  // Readback buffer sized for the texture footprint
  device->GetCopyableFootprints(&texture_desc, 0, 1, 0,
                                &gpu_blit_.readback_layout, nullptr, nullptr,
                                &gpu_blit_.readback_size);

  D3D12_RESOURCE_DESC buffer_desc;
  util::FillBufferResourceDesc(buffer_desc, gpu_blit_.readback_size,
                               D3D12_RESOURCE_FLAG_NONE);
  if (FAILED(device->CreateCommittedResource(
          &util::kHeapPropertiesReadback, D3D12_HEAP_FLAG_NONE, &buffer_desc,
          D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
          IID_PPV_ARGS(&gpu_blit_.readback_buffer)))) {
    XELOGE("D3D12Presenter: Failed to create GPU blit readback buffer");
    return false;
  }

  // Pre-allocate the converted pixel buffer
  gpu_blit_.converted_pixels.resize(size_t(w) * h * 4);
  gpu_blit_.width = w;
  gpu_blit_.height = h;
  gpu_blit_.fence_value = 0;
  return true;
}

bool D3D12Presenter::CaptureGuestOutputGPUBlit(const void*& data_out,
                                                uint32_t& width_out,
                                                uint32_t& height_out) {
  // Acquire guest output resource
  Microsoft::WRL::ComPtr<ID3D12Resource> guest_output_resource;
  {
    uint32_t guest_output_mailbox_index;
    auto guest_output_consumer_lease(
        ConsumeGuestOutput(guest_output_mailbox_index));
    if (guest_output_mailbox_index != UINT32_MAX) {
      guest_output_resource =
          guest_output_resources_[guest_output_mailbox_index].second;
    }
  }
  if (!guest_output_resource) {
    return false;
  }

  D3D12_RESOURCE_DESC texture_desc = xe_d3d12_resource_desc(guest_output_resource.Get());
  uint32_t w = uint32_t(texture_desc.Width);
  uint32_t h = uint32_t(texture_desc.Height);
  if (w == 0 || h == 0) return false;

  // Ensure persistent resources match current dimensions
  if (gpu_blit_.width != w || gpu_blit_.height != h) {
    DestroyGPUBlitResources();
    if (!CreateGPUBlitResources(w, h, texture_desc)) {
      DestroyGPUBlitResources();
      return false;
    }
  }

  // Wait for previous blit if still in flight
  if (gpu_blit_.fence_value > 0) {
    if (gpu_blit_.fence->GetCompletedValue() < gpu_blit_.fence_value) {
      gpu_blit_.fence->SetEventOnCompletion(gpu_blit_.fence_value, nullptr);
    }
  }

  // Record commands: copy guest output texture ??? readback buffer
  gpu_blit_.command_allocator->Reset();
  gpu_blit_.command_list->Reset(gpu_blit_.command_allocator.Get(), nullptr);

  D3D12_RESOURCE_BARRIER barrier;
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
  barrier.Transition.pResource = guest_output_resource.Get();
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore = kGuestOutputInternalState;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  if constexpr (kGuestOutputInternalState !=
                D3D12_RESOURCE_STATE_COPY_SOURCE) {
    gpu_blit_.command_list->ResourceBarrier(1, &barrier);
  }

  D3D12_TEXTURE_COPY_LOCATION copy_dest;
  copy_dest.pResource = gpu_blit_.readback_buffer.Get();
  copy_dest.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  copy_dest.PlacedFootprint = gpu_blit_.readback_layout;

  D3D12_TEXTURE_COPY_LOCATION copy_source;
  copy_source.pResource = guest_output_resource.Get();
  copy_source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  copy_source.SubresourceIndex = 0;
  gpu_blit_.command_list->CopyTextureRegion(&copy_dest, 0, 0, 0, &copy_source,
                                            nullptr);

  if constexpr (kGuestOutputInternalState !=
                D3D12_RESOURCE_STATE_COPY_SOURCE) {
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    gpu_blit_.command_list->ResourceBarrier(1, &barrier);
  }

  if (FAILED(gpu_blit_.command_list->Close())) {
    XELOGE("D3D12Presenter: Failed to close GPU blit command list");
    return false;
  }

  // Submit and wait
  ID3D12CommandQueue* const direct_queue = provider_.GetDirectQueue();
  ID3D12CommandList* execute_list = gpu_blit_.command_list.Get();
  direct_queue->ExecuteCommandLists(1, &execute_list);
  gpu_blit_.fence_value++;
  if (FAILED(direct_queue->Signal(gpu_blit_.fence.Get(),
                                   gpu_blit_.fence_value))) {
    XELOGE("D3D12Presenter: Failed to signal GPU blit fence");
    return false;
  }
  if (FAILED(gpu_blit_.fence->SetEventOnCompletion(gpu_blit_.fence_value,
                                                     nullptr))) {
    XELOGE("D3D12Presenter: Failed to await GPU blit fence");
    return false;
  }

  // Map, convert 10bpc???8bpc, unmap
  D3D12_RANGE read_range;
  read_range.Begin = gpu_blit_.readback_layout.Offset;
  read_range.End = gpu_blit_.readback_size;
  void* mapping;
  if (FAILED(gpu_blit_.readback_buffer->Map(0, &read_range, &mapping))) {
    XELOGE("D3D12Presenter: Failed to map GPU blit readback buffer");
    return false;
  }

  uint32_t* out_pixels =
      reinterpret_cast<uint32_t*>(gpu_blit_.converted_pixels.data());
  for (uint32_t y = 0; y < h; ++y) {
    uint32_t* dest_row = &out_pixels[size_t(w) * y];
    const uint32_t* source_row = reinterpret_cast<const uint32_t*>(
        reinterpret_cast<const uint8_t*>(mapping) +
        gpu_blit_.readback_layout.Offset +
        size_t(gpu_blit_.readback_layout.Footprint.RowPitch) * y);
    for (uint32_t x = 0; x < w; ++x) {
      dest_row[x] = Packed10bpcRGBTo8bpcBytes(source_row[x]);
    }
  }

  D3D12_RANGE written_range = {0, 0};
  gpu_blit_.readback_buffer->Unmap(0, &written_range);

  data_out = gpu_blit_.converted_pixels.data();
  width_out = w;
  height_out = h;
  return true;
}
#endif  // XENIA_LIBRETRO

bool D3D12Presenter::CaptureGuestOutput(RawImage& image_out) {
  Microsoft::WRL::ComPtr<ID3D12Resource> guest_output_resource;
  {
    uint32_t guest_output_mailbox_index;
    auto guest_output_consumer_lease(
        ConsumeGuestOutput(guest_output_mailbox_index));
    if (guest_output_mailbox_index != UINT32_MAX) {
      guest_output_resource =
          guest_output_resources_[guest_output_mailbox_index].second;
    }
    // Incremented the reference count of the guest output resource - safe to
    // leave the consumer critical section now.
  }
  if (!guest_output_resource) {
    return false;
  }

  ID3D12Device* device = provider_.GetDevice();

  D3D12_RESOURCE_DESC texture_desc = xe_d3d12_resource_desc(guest_output_resource.Get());
  D3D12_TEXTURE_COPY_LOCATION copy_dest;
  UINT64 copy_dest_size;
  device->GetCopyableFootprints(&texture_desc, 0, 1, 0,
                                &copy_dest.PlacedFootprint, nullptr, nullptr,
                                &copy_dest_size);

  D3D12_RESOURCE_DESC buffer_desc;
  util::FillBufferResourceDesc(buffer_desc, copy_dest_size,
                               D3D12_RESOURCE_FLAG_NONE);
  Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
  // Create zeroed not to leak data in the row padding.
  if (FAILED(device->CreateCommittedResource(
          &util::kHeapPropertiesReadback, D3D12_HEAP_FLAG_NONE, &buffer_desc,
          D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&buffer)))) {
    XELOGE("D3D12Presenter: Failed to create the guest output capture buffer");
    return false;
  }

  {
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> command_allocator;
    if (FAILED(
            device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                           IID_PPV_ARGS(&command_allocator)))) {
      XELOGE(
          "D3D12Presenter: Failed to create the guest output capturing command "
          "allocator");
      return false;
    }
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> command_list;
    if (FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         command_allocator.Get(), nullptr,
                                         IID_PPV_ARGS(&command_list)))) {
      XELOGE(
          "D3D12Presenter: Failed to create the guest output capturing command "
          "list");
      return false;
    }

    D3D12_RESOURCE_BARRIER barrier;
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = guest_output_resource.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = kGuestOutputInternalState;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    if constexpr (kGuestOutputInternalState !=
                  D3D12_RESOURCE_STATE_COPY_SOURCE) {
      command_list->ResourceBarrier(1, &barrier);
    }
    copy_dest.pResource = buffer.Get();
    copy_dest.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    D3D12_TEXTURE_COPY_LOCATION copy_source;
    copy_source.pResource = guest_output_resource.Get();
    copy_source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    copy_source.SubresourceIndex = 0;
    command_list->CopyTextureRegion(&copy_dest, 0, 0, 0, &copy_source, nullptr);
    if constexpr (kGuestOutputInternalState !=
                  D3D12_RESOURCE_STATE_COPY_SOURCE) {
      std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
      command_list->ResourceBarrier(1, &barrier);
    }
    if (FAILED(command_list->Close())) {
      XELOGE(
          "D3D12Presenter: Failed to close the guest output capturing command "
          "list");
      return false;
    }

    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    const HRESULT fence_create_result =
        device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    if (FAILED(fence_create_result)) {
      XELOGE(
          "D3D12Presenter: Failed to create the guest output capturing fence, "
          "result 0x{:08X}",
          fence_create_result);
      return false;
    }
    ID3D12CommandQueue* const direct_queue = provider_.GetDirectQueue();
    ID3D12CommandList* execute_command_list = command_list.Get();
    direct_queue->ExecuteCommandLists(1, &execute_command_list);
    const HRESULT fence_signal_result = direct_queue->Signal(fence.Get(), 1);
    if (FAILED(fence_signal_result)) {
      XELOGE(
          "D3D12Presenter: Failed to enqueue signaling of the guest output "
          "capturing fence, result 0x{:08X}",
          fence_signal_result);
      return false;
    }
    const HRESULT fence_wait_result = fence->SetEventOnCompletion(1, nullptr);
    if (FAILED(fence_wait_result)) {
      XELOGE(
          "D3D12Presenter: Failed to await the guest output capturing fence, "
          "result 0x{:08X}",
          fence_wait_result);
      return false;
    }
  }

  D3D12_RANGE read_range;
  read_range.Begin = copy_dest.PlacedFootprint.Offset;
  read_range.End = copy_dest_size;
  void* mapping;
  if (FAILED(buffer->Map(0, &read_range, &mapping))) {
    XELOGE("D3D12Presenter: Failed to map the guest output capture buffer");
    return false;
  }
  image_out.width = uint32_t(texture_desc.Width);
  image_out.height = uint32_t(texture_desc.Height);
  image_out.stride = sizeof(uint32_t) * image_out.width;
  image_out.data.resize(image_out.stride * image_out.height);
  uint32_t* image_out_pixels =
      reinterpret_cast<uint32_t*>(image_out.data.data());
  for (uint32_t y = 0; y < image_out.height; ++y) {
    uint32_t* dest_row = &image_out_pixels[size_t(image_out.width) * y];
    const uint32_t* source_row = reinterpret_cast<const uint32_t*>(
        reinterpret_cast<const uint8_t*>(mapping) +
        copy_dest.PlacedFootprint.Offset +
        size_t(copy_dest.PlacedFootprint.Footprint.RowPitch) * y);
    for (uint32_t x = 0; x < image_out.width; ++x) {
      dest_row[x] = Packed10bpcRGBTo8bpcBytes(source_row[x]);
    }
  }
  // Unmapping will be done implicitly when the resource goes out of scope and
  // gets destroyed.
  return true;
}

bool D3D12Presenter::RefreshGuestOutputImpl(
    uint32_t mailbox_index, uint32_t frontbuffer_width,
    uint32_t frontbuffer_height,
    GuestOutputRefresher refresher, void* refresher_data,
    bool& is_8bpc_out_ref) {
  assert_not_zero(frontbuffer_width);
  assert_not_zero(frontbuffer_height);
  std::pair<uint64_t, Microsoft::WRL::ComPtr<ID3D12Resource>>&
      guest_output_resource_ref = guest_output_resources_[mailbox_index];
  if (guest_output_resource_ref.second) {
    D3D12_RESOURCE_DESC guest_output_resource_current_desc =
        xe_d3d12_resource_desc(guest_output_resource_ref.second.Get());
    if (guest_output_resource_current_desc.Width != frontbuffer_width ||
        guest_output_resource_current_desc.Height != frontbuffer_height) {
      // Capture holds the mailbox consumer lease and awaits its own submissions.
      guest_output_resource_refresher_completion_timeline_
          ->AwaitSubmissionAndUpdateCompleted(guest_output_resource_ref.first);
      guest_output_resource_ref.second.Reset();
    }
  }
  if (!guest_output_resource_ref.second) {
    ID3D12Device* device = provider_.GetDevice();
    D3D12_RESOURCE_DESC guest_output_resource_new_desc;
    guest_output_resource_new_desc.Dimension =
        D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    guest_output_resource_new_desc.Alignment = 0;
    guest_output_resource_new_desc.Width = frontbuffer_width;
    guest_output_resource_new_desc.Height = frontbuffer_height;
    guest_output_resource_new_desc.DepthOrArraySize = 1;
    guest_output_resource_new_desc.MipLevels = 1;
    guest_output_resource_new_desc.Format = kGuestOutputFormat;
    guest_output_resource_new_desc.SampleDesc.Count = 1;
    guest_output_resource_new_desc.SampleDesc.Quality = 0;
    guest_output_resource_new_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    guest_output_resource_new_desc.Flags =
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (FAILED(device->CreateCommittedResource(
            &util::kHeapPropertiesDefault,
            provider_.GetHeapFlagCreateNotZeroed(),
            &guest_output_resource_new_desc, kGuestOutputInternalState, nullptr,
            IID_PPV_ARGS(&guest_output_resource_ref.second)))) {
      XELOGE("D3D12Presenter: Failed to create the guest output {}x{} texture",
             frontbuffer_width, frontbuffer_height);
      return false;
    }
  }
  D3D12GuestOutputRefreshContext context(
      is_8bpc_out_ref, guest_output_resource_ref.second.Get());
  bool refresher_succeeded = refresher(refresher_data, context);
  // Even if the refresher has returned false, it still might have submitted
  // some commands referencing the resource. It's better to put an excessive
  // signal and wait slightly longer, for nothing important, while shutting down
  // than to destroy the resource while it's still in use.
  guest_output_resource_ref.first =
      guest_output_resource_refresher_completion_timeline_
          ->GetUpcomingSubmission();
  guest_output_resource_refresher_completion_timeline_->SignalAndAdvance(
      provider_.GetDirectQueue());
  return refresher_succeeded;
}

bool D3D12Presenter::InitializeSurfaceIndependent() {
  guest_output_resource_refresher_completion_timeline_ =
      D3D12GPUCompletionTimeline::Create(provider_.GetDevice());
  return guest_output_resource_refresher_completion_timeline_ != nullptr;
}
}  // namespace d3d12
}  // namespace ui
}  // namespace xe
