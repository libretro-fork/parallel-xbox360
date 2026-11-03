#define COBJMACROS
#include "core_d3d12_fence.h"

int xe_d3d12_wait_fence(ID3D12Fence *fence, UINT64 value, HANDLE event,
                        unsigned timeout_ms) {
  UINT64 completed;
  ULONGLONG started, elapsed;
  DWORD result, remaining;
  if (!fence || !value) return XE_D3D12_FENCE_READY;
  completed = ID3D12Fence_GetCompletedValue(fence);
  if (completed == ~(UINT64)0) return XE_D3D12_FENCE_DEVICE_LOST;
  if (completed >= value) return XE_D3D12_FENCE_READY;
  if (!timeout_ms) return XE_D3D12_FENCE_TIMEOUT;
  if (!event) return XE_D3D12_FENCE_FAILED;
  started = GetTickCount64();
  if (FAILED(ID3D12Fence_SetEventOnCompletion(fence, value, event)))
    return XE_D3D12_FENCE_FAILED;
  for (;;) {
    elapsed = GetTickCount64() - started;
    remaining = elapsed >= timeout_ms ? 0 : timeout_ms - (DWORD)elapsed;
    result = WaitForSingleObject(event, remaining);
    completed = ID3D12Fence_GetCompletedValue(fence);
    if (completed == ~(UINT64)0) return XE_D3D12_FENCE_DEVICE_LOST;
    if (completed >= value) return XE_D3D12_FENCE_READY;
    if (result == WAIT_FAILED) return XE_D3D12_FENCE_FAILED;
    if (result == WAIT_TIMEOUT || !remaining)
      return XE_D3D12_FENCE_TIMEOUT;
    /* A stale auto-reset event must not stand in for completion of this value. */
  }
}
