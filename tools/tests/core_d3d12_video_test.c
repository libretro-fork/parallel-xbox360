/* Real-device oracle for the production C89 D3D12 video driver. */
#define COBJMACROS
#include "core_d3d12_video.h"
#include <stdio.h>
#include <string.h>
#include <stddef.h>
struct held_frame {
  ID3D12Resource *texture;
  ID3D12Fence *fence;
  UINT64 value;
  unsigned tag, width, height;
  D3D12_RESOURCE_STATES state;
};
struct frontend_state {
  struct retro_hw_render_interface_d3d12 api;
  struct held_frame held[32];
  ID3D12Fence *completion;
  HANDLE event;
  UINT64 value;
  unsigned index, mask, tag, width, height;
  unsigned handoffs, pictures, reads, waits;
  int error, expect_fenced;
};
static struct frontend_state front;
#define HR(expression) do { if (FAILED(expression)) goto failed; } while (0)
#define RELEASE(type, object) do { if (object) { type##_Release(object); object = NULL; } } while (0)
static void consume(struct held_frame *held) {
  ID3D12Resource *readback = NULL;
  ID3D12CommandAllocator *allocator = NULL;
  ID3D12GraphicsCommandList *list = NULL;
  ID3D12CommandList *execute;
  D3D12_RESOURCE_DESC desc;
  D3D12_HEAP_PROPERTIES heap;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
  D3D12_TEXTURE_COPY_LOCATION dst, src;
  D3D12_RESOURCE_BARRIER barrier;
  D3D12_RANGE range;
  UINT64 size;
  void *mapped = NULL;
  unsigned row, byte;
  uint8_t expected;
  if (!held->texture) return;
  memset(&desc, 0, sizeof(desc));
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = held->width; desc.Height = held->height;
  desc.DepthOrArraySize = desc.MipLevels = 1; desc.SampleDesc.Count = 1;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  ID3D12Device_GetCopyableFootprints(front.api.device, &desc, 0, 1, 0, &footprint, NULL, NULL, &size);
  memset(&desc, 0, sizeof(desc));
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = size; desc.Height = 1; desc.DepthOrArraySize = desc.MipLevels = 1;
  desc.SampleDesc.Count = 1; desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  memset(&heap, 0, sizeof(heap)); heap.Type = D3D12_HEAP_TYPE_READBACK;
  HR(ID3D12Device_CreateCommittedResource(front.api.device, &heap, D3D12_HEAP_FLAG_NONE,
      &desc, D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void **)&readback));
  HR(ID3D12Device_CreateCommandAllocator(front.api.device, D3D12_COMMAND_LIST_TYPE_DIRECT,
      &IID_ID3D12CommandAllocator, (void **)&allocator));
  HR(ID3D12Device_CreateCommandList(front.api.device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
      allocator, NULL, &IID_ID3D12GraphicsCommandList, (void **)&list));
  memset(&barrier, 0, sizeof(barrier));
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = held->texture;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  if (held->state != D3D12_RESOURCE_STATE_COPY_SOURCE) {
    barrier.Transition.StateBefore = held->state;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &barrier);
  }
  memset(&dst, 0, sizeof(dst)); dst.pResource = readback;
  dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; dst.PlacedFootprint = footprint;
  memset(&src, 0, sizeof(src)); src.pResource = held->texture;
  src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  ID3D12GraphicsCommandList_CopyTextureRegion(list, &dst, 0, 0, 0, &src, NULL);
  if (held->state != D3D12_RESOURCE_STATE_COPY_SOURCE) {
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.StateAfter = held->state;
    ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &barrier);
  }
  HR(ID3D12GraphicsCommandList_Close(list));
  if (held->fence) HR(ID3D12CommandQueue_Wait(front.api.queue, held->fence, held->value));
  execute = (ID3D12CommandList *)list;
  ID3D12CommandQueue_ExecuteCommandLists(front.api.queue, 1, &execute);
  HR(ID3D12CommandQueue_Signal(front.api.queue, front.completion, ++front.value));
  if (xe_d3d12_wait_fence(front.completion, front.value, front.event, 5000) != XE_D3D12_FENCE_READY) goto failed;
  range.Begin = 0; range.End = (SIZE_T)size;
  HR(ID3D12Resource_Map(readback, 0, &range, &mapped));
  for (row = 0; row < held->height; ++row) for (byte = 0; byte < held->width * 4; ++byte) {
    expected = (uint8_t)(held->tag + row * 17 + byte * 3);
    if (((uint8_t *)mapped)[footprint.Offset + (size_t)row * footprint.Footprint.RowPitch + byte] != expected) goto failed;
  }
  ID3D12Resource_Unmap(readback, 0, NULL); mapped = NULL;
  ++front.reads;
  goto done;
failed:
  front.error = 1;
  if (mapped) ID3D12Resource_Unmap(readback, 0, NULL);
done:
  RELEASE(ID3D12Resource, readback);
  RELEASE(ID3D12GraphicsCommandList, list);
  RELEASE(ID3D12CommandAllocator, allocator);
  RELEASE(ID3D12Resource, held->texture);
  RELEASE(ID3D12Fence, held->fence);
}
static void retain(ID3D12Resource *texture, ID3D12Fence *fence, UINT64 value) {
  struct held_frame *held = &front.held[front.expect_fenced ? front.index : 0];
  if (held->texture) front.error = 2;
  held->texture = texture; held->fence = fence; held->value = value;
  held->tag = front.tag; held->width = front.width; held->height = front.height;
  held->state = front.api.required_state;
  ID3D12Resource_AddRef(texture); if (fence) ID3D12Fence_AddRef(fence);
  ++front.handoffs;
}
static void legacy(void *handle, ID3D12Resource *texture, DXGI_FORMAT format) {
  if (handle != &front || format != DXGI_FORMAT_R8G8B8A8_UNORM || front.expect_fenced) front.error = 3;
  retain(texture, NULL, 0);
}
static void fenced(void *handle, ID3D12Resource *texture, DXGI_FORMAT format, ID3D12Fence *fence, UINT64 value) {
  if (handle != &front || format != DXGI_FORMAT_R8G8B8A8_UNORM || !front.expect_fenced || !fence || !value) front.error = 4;
  retain(texture, fence, value);
}
static unsigned index_of(void *handle) { (void)handle; return front.index; }
static unsigned mask_of(void *handle) { (void)handle; return front.mask; }
static void wait_index(void *handle) {
  if (handle != &front) front.error = 5;
  ++front.waits; consume(&front.held[front.index]);
}
static void RETRO_CALLCONV refresh(const void *data, unsigned width, unsigned height, size_t pitch) {
  if (data != RETRO_HW_FRAME_BUFFER_VALID || width != front.width || height != front.height || pitch) front.error = 6;
  ++front.pictures;
  if (!front.expect_fenced) consume(&front.held[0]);
}
static void pixels(uint8_t *data, unsigned tag, unsigned width, unsigned height) {
  unsigned row, byte;
  for (row = 0; row < height; ++row) for (byte = 0; byte < width * 4; ++byte)
    data[(size_t)row * width * 4 + byte] = (uint8_t)(tag + row * 17 + byte * 3);
}
int main(void) {
  xe_d3d12_video video;
  ID3D12Fence *gate = NULL;
  D3D12_COMMAND_QUEUE_DESC queue;
  uint8_t data[65 * 4 * 4];
  unsigned mode, frame, i, before, reads_before, pictures_before;
  SYSTEM_INFO system_info;
  void *guarded;
  DWORD old_protection;
  size_t legacy_size;
  struct retro_hw_render_interface_d3d12 *legacy_api;
  static const unsigned slots[6] = {0, 3, 1, 0, 3, 1};
  static const unsigned widths[6] = {13, 64, 65, 64, 13, 65};
  memset(&video, 0, sizeof(video)); memset(&front, 0, sizeof(front));
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  if (FAILED(D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0,
      &IID_ID3D12Device, (void **)&front.api.device))) return 1;
  memset(&queue, 0, sizeof(queue)); queue.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  if (FAILED(ID3D12Device_CreateCommandQueue(front.api.device, &queue,
      &IID_ID3D12CommandQueue, (void **)&front.api.queue))) return 2;
  if (FAILED(ID3D12Device_CreateFence(front.api.device, 0, D3D12_FENCE_FLAG_NONE,
      &IID_ID3D12Fence, (void **)&front.completion))) return 3;
  front.event = CreateEventA(NULL, FALSE, FALSE, NULL); if (!front.event) return 4;
  /* A stale signal must not impersonate completion of a newer fence value. */
  SetEvent(front.event);
  if (xe_d3d12_wait_fence(front.completion, 1, front.event, 1) != XE_D3D12_FENCE_TIMEOUT) return 5;
  ID3D12Fence_Signal(front.completion, 1); front.value = 1;
  front.api.interface_type = RETRO_HW_RENDER_INTERFACE_D3D12;
  front.api.handle = &front; front.api.set_texture = legacy;
  front.mask = 0xbu;
  GetSystemInfo(&system_info);
  guarded = VirtualAlloc(NULL, (size_t)system_info.dwPageSize * 2,
      MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
  if (!guarded || !VirtualProtect((uint8_t *)guarded + system_info.dwPageSize,
      system_info.dwPageSize, PAGE_NOACCESS, &old_protection)) return 17;
  legacy_size = offsetof(struct retro_hw_render_interface_d3d12, get_sync_index);
  legacy_api = (struct retro_hw_render_interface_d3d12 *)
      ((uint8_t *)guarded + system_info.dwPageSize - legacy_size);
  for (mode = 0; mode < 4; ++mode) {
    front.api.interface_version = mode ? 2 : 1;
    front.expect_fenced = mode == 1 || mode == 2;
    front.api.get_sync_index = index_of;
    front.api.get_sync_index_mask = mask_of;
    front.api.wait_sync_index = wait_index;
    front.api.set_texture_fenced = mode == 3 ? NULL : fenced;
    front.api.required_state = mode == 2 ? D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE : D3D12_RESOURCE_STATE_COPY_SOURCE;
    if (!mode) memcpy(legacy_api, &front.api, legacy_size);
    if (!xe_d3d12_video_bind(&video, mode ? &front.api : legacy_api)) return 6;
    before = front.waits; reads_before = front.reads; pictures_before = front.pictures;
    for (frame = 0; frame < 6; ++frame) {
      front.index = slots[frame]; front.tag = mode * 31 + frame * 7;
      front.width = widths[frame]; front.height = 3;
      pixels(data, front.tag, front.width, front.height);
      if (!xe_d3d12_video_present(&video, data, front.width, front.height, refresh)) return 7;
    }
    if (!xe_d3d12_video_detach(&video, 5000)) return 8;
    for (i = 0; i < 32; ++i) consume(&front.held[i]);
    if (front.error || front.pictures != pictures_before + 6 || front.reads != reads_before + 6 ||
        front.waits != before + (front.expect_fenced ? 6u : 0u)) return 9;
  }
  /* A blocked frontend queue must not make detach free in-flight resources. */
  front.api.set_texture_fenced = fenced; front.expect_fenced = 1;
  front.api.required_state = D3D12_RESOURCE_STATE_COPY_SOURCE;
  if (!xe_d3d12_video_bind(&video, &front.api)) return 10;
  if (FAILED(ID3D12Device_CreateFence(front.api.device, 0, D3D12_FENCE_FLAG_NONE,
      &IID_ID3D12Fence, (void **)&gate))) return 11;
  if (FAILED(ID3D12CommandQueue_Wait(front.api.queue, gate, 1))) return 12;
  front.index = 0; front.tag = 213; front.width = 13; front.height = 4;
  pixels(data, front.tag, front.width, front.height);
  if (!xe_d3d12_video_present(&video, data, front.width, front.height, refresh)) return 13;
  if (xe_d3d12_video_detach(&video, 1)) return 14;
  ID3D12Fence_Signal(gate, 1); consume(&front.held[0]);
  if (!xe_d3d12_video_detach(&video, 5000) || front.error) return 15;
  if (FAILED(ID3D12Device_GetDeviceRemovedReason(front.api.device))) return 16;
  RELEASE(ID3D12Fence, gate); RELEASE(ID3D12Fence, front.completion);
  VirtualFree(guarded, 0, MEM_RELEASE);
  CloseHandle(front.event);
  RELEASE(ID3D12CommandQueue, front.api.queue);
  RELEASE(ID3D12Device, front.api.device);
  puts("PASS: 25 GPU byte-checked v1/v2 frames, deferred frontend ownership, resize/state changes, stale events and bounded blocked-queue teardown");
  return 0;
}
