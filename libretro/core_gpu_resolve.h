/* Scaled guest-memory views. No allocation, mapping, or content movement. */
#ifndef XE_CORE_GPU_RESOLVE_H
#define XE_CORE_GPU_RESOLVE_H
#include <stdint.h>
static int xe_gpu_resolve_range(uint32_t start, uint32_t length,
      uint32_t scale_area, unsigned alignment_log2, uint64_t capacity,
      uint64_t *offset_out, uint64_t *length_out)
{
   const uint32_t guest_size = UINT32_C(1) << 29;
   uint64_t offset, size, mask, address_size;
   if (!scale_area || alignment_log2 >= 64 || start > guest_size ||
         length > guest_size - start || !offset_out || !length_out) return 0;
   mask = ((uint64_t)1 << alignment_log2) - 1;
   offset = (uint64_t)start * scale_area;
   size = (uint64_t)length * scale_area;
   address_size = (uint64_t)guest_size * scale_area;
   if (size > UINT64_MAX - mask) return 0;
   size = (size + mask) & ~mask;
   if (offset > address_size || size > address_size - offset ||
         offset > capacity || size > capacity - offset) return 0;
   *offset_out = offset;
   *length_out = size;
   return 1;
}
#endif
