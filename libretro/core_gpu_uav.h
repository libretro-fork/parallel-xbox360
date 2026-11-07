/* GPU-owner UAV write epochs; fixed MSVC C89 storage, no allocation/locks. */
#ifndef XE_CORE_GPU_UAV_H
#define XE_CORE_GPU_UAV_H
#include <stdint.h>
#define XE_GPU_UAV_SPANS 32
#define XE_GPU_UAV_APERTURE UINT32_C(0x20000000)
typedef struct xe_gpu_uav_span { uint32_t first, end; } xe_gpu_uav_span;
typedef struct xe_gpu_uav {
   xe_gpu_uav_span spans[XE_GPU_UAV_SPANS];
   unsigned count, unknown;
   uint32_t resource_state;
} xe_gpu_uav;
#ifdef __cplusplus
extern "C" {
#endif
/* Initialize the resource state. Clear history only when an ordering transition/alias barrier is
 * recorded, across ExecuteCommandLists, or at quiescent resource lifetime boundaries. Spans use
 * guest physical bytes, independent of the scaled window's GPU VA. */
void xe_gpu_uav_init(xe_gpu_uav *state, uint32_t resource_state);
void xe_gpu_uav_clear(xe_gpu_uav *state);
/* Prepare before recording a GPU operation. Return 1 if an UAV barrier must
 * precede it, and retain this operation's write span for the next operation.
 * A real state transition orders prior access; SRV states retain no writes.
 * Footprints include whole 128-byte scaled groups relative to group_base.
 * Bad spans and capacity exhaustion conservatively require an ordering barrier
 * on the GPU, never a CPU wait. Caller must emit the returned GPU barrier. */
int xe_gpu_uav_prepare(xe_gpu_uav *state, uint32_t next_state, int next_is_uav,
      uint32_t first, uint32_t length, uint32_t group_base);
#ifdef __cplusplus
}
#endif
#endif
