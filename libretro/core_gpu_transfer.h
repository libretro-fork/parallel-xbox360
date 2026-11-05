/* Borrowed transfer metadata. The owner must keep transfer records immutable
 * until command encoding ends. Stencil invocations ignore the optional host
 * depth source without modifying or duplicating the underlying record. */
#ifndef XE_CORE_GPU_TRANSFER_H
#define XE_CORE_GPU_TRANSFER_H
#include <stdint.h>
#include <stddef.h>
typedef struct xe_gpu_transfer_ref {
   const void *transfer;
   uint32_t shader_key;
   uint32_t stencil_bits;
} xe_gpu_transfer_ref;
static void xe_gpu_transfer_ref_set(xe_gpu_transfer_ref *ref,
      const void *transfer, uint32_t key, int stencil_bits)
{
   ref->transfer = transfer;
   ref->shader_key = key;
   ref->stencil_bits = stencil_bits != 0;
}
static const void *xe_gpu_transfer_host_depth(const xe_gpu_transfer_ref *ref,
      const void *host_depth)
{
   return ref->stencil_bits ? NULL : host_depth;
}
static int xe_gpu_transfer_mergeable(const xe_gpu_transfer_ref *a,
      const void *source_a, const void *host_a, const xe_gpu_transfer_ref *b,
      const void *source_b, const void *host_b)
{
   return a->shader_key == b->shader_key && source_a == source_b &&
      xe_gpu_transfer_host_depth(a,host_a) == xe_gpu_transfer_host_depth(b,host_b);
}
#endif
