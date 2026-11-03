/* Bounded GPU completion waits; MSVC C89. */
#ifndef PARALLEL_XBOX360_D3D12_FENCE_H
#define PARALLEL_XBOX360_D3D12_FENCE_H
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d12.h>
#ifdef __cplusplus
extern "C" {
#endif
enum xe_d3d12_fence_status {
  XE_D3D12_FENCE_FAILED = -1,
  XE_D3D12_FENCE_TIMEOUT = 0,
  XE_D3D12_FENCE_READY = 1,
  XE_D3D12_FENCE_DEVICE_LOST = 2
};
int xe_d3d12_wait_fence(ID3D12Fence *fence, UINT64 value, HANDLE event,
                        unsigned timeout_ms);
#ifdef __cplusplus
}
#endif
#endif
