#define COBJMACROS
#include "core_d3d12_video.h"
#include <string.h>
#include <stdint.h>

#define RELEASE(type, pointer) do { if (pointer) { type##_Release(pointer); pointer = NULL; } } while (0)

static int release_frame(struct xe_d3d12_video_frame *frame, unsigned timeout_ms) {
  if (xe_d3d12_wait_fence(frame->fence, frame->value, frame->event, timeout_ms) <= 0)
    return 0;
  if (frame->mapped && frame->upload) ID3D12Resource_Unmap(frame->upload, 0, NULL);
  RELEASE(ID3D12GraphicsCommandList, frame->commands);
  RELEASE(ID3D12CommandAllocator, frame->allocator);
  RELEASE(ID3D12Fence, frame->fence);
  if (frame->event) CloseHandle(frame->event);
  RELEASE(ID3D12Resource, frame->upload);
  RELEASE(ID3D12Resource, frame->texture);
  memset(frame, 0, sizeof(*frame));
  return 1;
}

void xe_d3d12_video_negotiate(retro_environment_t environment) {
  struct retro_hw_render_context_negotiation_interface probe;
  static const struct retro_hw_render_context_negotiation_interface_d3d12 request = {
    RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_D3D12,
    RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_D3D12_VERSION,
    RETRO_HW_RENDER_INTERFACE_D3D12_VERSION_2
  };
  if (!environment) return;
  probe.interface_type = RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_D3D12;
  probe.interface_version = 0;
  if (environment(RETRO_ENVIRONMENT_GET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_SUPPORT,
        &probe) && probe.interface_version >= 1)
    environment(RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE,
        (void *)&request);
}

int xe_d3d12_video_bind(xe_d3d12_video *video,
    const struct retro_hw_render_interface_d3d12 *frontend) {
  video->frontend = NULL;
  video->fenced = 0;
  ++video->generation;
  if (!frontend || frontend->interface_type != RETRO_HW_RENDER_INTERFACE_D3D12 ||
      frontend->interface_version < 1 || !frontend->device || !frontend->queue)
    return 0;
  /* Never inspect v2 members of a v1 allocation. */
  if (frontend->interface_version >= 2)
    video->fenced = frontend->get_sync_index && frontend->get_sync_index_mask &&
        frontend->wait_sync_index && frontend->set_texture_fenced;
  if (!video->fenced && !frontend->set_texture) return 0;
  video->frontend = frontend;
  return 1;
}

int xe_d3d12_video_detach(xe_d3d12_video *video, unsigned timeout_ms) {
  ULONGLONG start, elapsed;
  unsigned i, remaining;
  int complete = 1;
  video->frontend = NULL;
  video->fenced = 0;
  video->legacy_slot = 0;
  ++video->generation;
  start = GetTickCount64();
  for (i = 0; i < XE_D3D12_VIDEO_SLOTS; ++i) {
    elapsed = GetTickCount64() - start;
    remaining = elapsed >= timeout_ms ? 0 : timeout_ms - (unsigned)elapsed;
    if (!release_frame(&video->frames[i], remaining)) complete = 0;
  }
  return complete;
}

static HRESULT create_frame(struct xe_d3d12_video_frame *frame,
    ID3D12Device *device, unsigned width, unsigned height) {
  HRESULT hr;
  D3D12_RESOURCE_DESC desc;
  D3D12_HEAP_PROPERTIES heap;
  D3D12_RANGE read;
  UINT64 upload_size;
  frame->device = device;
  hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
      &IID_ID3D12CommandAllocator, (void **)&frame->allocator);
  if (FAILED(hr)) return hr;
  hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
      frame->allocator, NULL, &IID_ID3D12GraphicsCommandList, (void **)&frame->commands);
  if (FAILED(hr)) return hr;
  hr = ID3D12GraphicsCommandList_Close(frame->commands);
  if (FAILED(hr)) return hr;
  hr = ID3D12Device_CreateFence(device, 0, D3D12_FENCE_FLAG_NONE,
      &IID_ID3D12Fence, (void **)&frame->fence);
  if (FAILED(hr)) return hr;
  frame->event = CreateEventA(NULL, FALSE, FALSE, NULL);
  if (!frame->event) return HRESULT_FROM_WIN32(GetLastError());
  memset(&desc, 0, sizeof(desc));
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = width; desc.Height = height;
  desc.DepthOrArraySize = 1; desc.MipLevels = 1;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  memset(&heap, 0, sizeof(heap)); heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  hr = ID3D12Device_CreateCommittedResource(device, &heap, D3D12_HEAP_FLAG_NONE,
      &desc, D3D12_RESOURCE_STATE_COPY_DEST, NULL,
      &IID_ID3D12Resource, (void **)&frame->texture);
  if (FAILED(hr)) return hr;
  ID3D12Device_GetCopyableFootprints(device, &desc, 0, 1, 0,
      &frame->layout, NULL, NULL, &upload_size);
  memset(&desc, 0, sizeof(desc));
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = upload_size; desc.Height = 1;
  desc.DepthOrArraySize = 1; desc.MipLevels = 1;
  desc.SampleDesc.Count = 1; desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  heap.Type = D3D12_HEAP_TYPE_UPLOAD;
  hr = ID3D12Device_CreateCommittedResource(device, &heap, D3D12_HEAP_FLAG_NONE,
      &desc, D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
      &IID_ID3D12Resource, (void **)&frame->upload);
  if (FAILED(hr)) return hr;
  read.Begin = read.End = 0;
  hr = ID3D12Resource_Map(frame->upload, 0, &read, &frame->mapped);
  if (FAILED(hr)) return hr;
  frame->width = width; frame->height = height;
  frame->state = D3D12_RESOURCE_STATE_COPY_DEST;
  return S_OK;
}

int xe_d3d12_video_present(xe_d3d12_video *video, const void *pixels,
    unsigned width, unsigned height, retro_video_refresh_t refresh) {
  const struct retro_hw_render_interface_d3d12 *frontend = video->frontend;
  struct xe_d3d12_video_frame *frame;
  unsigned slot, mask, row, generation = video->generation;
  size_t pitch;
  uint8_t *dst;
  const uint8_t *src = (const uint8_t *)pixels;
  D3D12_RESOURCE_BARRIER barrier;
  D3D12_TEXTURE_COPY_LOCATION destination, source;
  ID3D12CommandList *commands;
  HRESULT hr;
  if (!frontend || !pixels || !width || !height || !refresh ||
      (size_t)height > SIZE_MAX / 4 / width)
    return 0;
  if (video->fenced) {
    slot = frontend->get_sync_index(frontend->handle);
    mask = frontend->get_sync_index_mask(frontend->handle);
    if (slot >= XE_D3D12_VIDEO_SLOTS || !(mask & (1u << slot))) return 0;
    frontend->wait_sync_index(frontend->handle);
  } else {
    slot = video->legacy_slot;
    video->legacy_slot = (slot + 1) % 2;
  }
  frame = &video->frames[slot];
  if (frame->device != frontend->device || frame->width != width || frame->height != height) {
    if (!release_frame(frame, XE_D3D12_VIDEO_TIMEOUT_MS)) return 0;
    hr = create_frame(frame, frontend->device, width, height);
    if (FAILED(hr)) {
      video->last_error = hr; release_frame(frame, 0); return 0;
    }
  }
  if (xe_d3d12_wait_fence(frame->fence, frame->value, frame->event,
        XE_D3D12_VIDEO_TIMEOUT_MS) <= 0) return 0;
  pitch = (size_t)width * 4;
  dst = (uint8_t *)frame->mapped + frame->layout.Offset;
  if (pitch == frame->layout.Footprint.RowPitch)
    memcpy(dst, src, pitch * height);
  else for (row = 0; row < height; ++row)
    memcpy(dst + (size_t)row * frame->layout.Footprint.RowPitch,
        src + (size_t)row * pitch, pitch);
  hr = ID3D12CommandAllocator_Reset(frame->allocator);
  if (FAILED(hr)) { video->last_error = hr; return 0; }
  hr = ID3D12GraphicsCommandList_Reset(frame->commands, frame->allocator, NULL);
  if (FAILED(hr)) { video->last_error = hr; return 0; }
  memset(&barrier, 0, sizeof(barrier));
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = frame->texture;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  if (frame->state != D3D12_RESOURCE_STATE_COPY_DEST) {
    barrier.Transition.StateBefore = frame->state;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    ID3D12GraphicsCommandList_ResourceBarrier(frame->commands, 1, &barrier);
  }
  memset(&destination, 0, sizeof(destination));
  destination.pResource = frame->texture;
  destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  memset(&source, 0, sizeof(source));
  source.pResource = frame->upload;
  source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  source.PlacedFootprint = frame->layout;
  ID3D12GraphicsCommandList_CopyTextureRegion(frame->commands,
      &destination, 0, 0, 0, &source, NULL);
  if (frontend->required_state != D3D12_RESOURCE_STATE_COPY_DEST) {
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = frontend->required_state;
    ID3D12GraphicsCommandList_ResourceBarrier(frame->commands, 1, &barrier);
  }
  hr = ID3D12GraphicsCommandList_Close(frame->commands);
  if (FAILED(hr)) { video->last_error = hr; return 0; }
  commands = (ID3D12CommandList *)frame->commands;
  ID3D12CommandQueue_ExecuteCommandLists(frontend->queue, 1, &commands);
  frame->state = frontend->required_state;
  ++frame->value;
  hr = ID3D12CommandQueue_Signal(frontend->queue, frame->fence, frame->value);
  if (FAILED(hr)) { video->last_error = hr; return 0; }
  if (video->fenced)
    frontend->set_texture_fenced(frontend->handle, frame->texture,
        DXGI_FORMAT_R8G8B8A8_UNORM, frame->fence, frame->value);
  else
    frontend->set_texture(frontend->handle, frame->texture, DXGI_FORMAT_R8G8B8A8_UNORM);
  refresh(RETRO_HW_FRAME_BUFFER_VALID, width, height, 0);
  if (!video->fenced && video->generation == generation) {
    /* Include the frontend's v1 copy-before-return in slot retirement. A
     * context rebuild during refresh has already retired the old binding. */
    ++frame->value;
    video->last_error = ID3D12CommandQueue_Signal(frontend->queue, frame->fence, frame->value);
  }
  return 1;
}
