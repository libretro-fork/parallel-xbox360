/* Frontend D3D12 frame ownership and v1/v2 handoff; MSVC C89. */
#ifndef PARALLEL_XBOX360_D3D12_VIDEO_H
#define PARALLEL_XBOX360_D3D12_VIDEO_H
#include "libretro.h"
#include "core_d3d12_fence.h"
#include "libretro_d3d12.h"
#ifdef __cplusplus
extern "C" {
#endif
#define XE_D3D12_VIDEO_SLOTS 32
#define XE_D3D12_VIDEO_TIMEOUT_MS 5000
struct xe_d3d12_video_frame {
  ID3D12Device *device;
  ID3D12Resource *texture, *upload;
  void *mapped;
  ID3D12CommandAllocator *allocator;
  ID3D12GraphicsCommandList *commands;
  ID3D12Fence *fence;
  HANDLE event;
  UINT64 value;
  unsigned width, height;
  D3D12_RESOURCE_STATES state;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout;
};
typedef struct xe_d3d12_video {
  const struct retro_hw_render_interface_d3d12 *frontend;
  struct xe_d3d12_video_frame frames[XE_D3D12_VIDEO_SLOTS];
  unsigned legacy_slot, generation;
  int fenced;
  HRESULT last_error;
} xe_d3d12_video;
void xe_d3d12_video_negotiate(retro_environment_t environment);
int xe_d3d12_video_bind(xe_d3d12_video *video,
    const struct retro_hw_render_interface_d3d12 *frontend);
/* Detach borrowed callbacks first, then retire frames within a shared budget.
 * A stalled GPU retains its slot's COM references/event rather than freeing
 * resources without completion proof. It can be retried while the core lives. */
int xe_d3d12_video_detach(xe_d3d12_video *video, unsigned timeout_ms);
/* On success exactly one refresh is called. Failure calls none; the caller
 * supplies its ordinary duplicate frame so retro_run still presents once. */
int xe_d3d12_video_present(xe_d3d12_video *video, const void *pixels,
    unsigned width, unsigned height, retro_video_refresh_t refresh);
#ifdef __cplusplus
}
#endif
#endif
