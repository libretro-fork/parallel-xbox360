/* Cold raw-SPIR-V texture-store rewrite. No shader-language parser. C89. */
#ifndef XE_CORE_SPV_IMAGE_SINK_H
#define XE_CORE_SPV_IMAGE_SINK_H
#include "core_spv_binary.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xe_spv_sink_entry {
  uint32_t offset, flags;
} xe_spv_sink_entry;
typedef struct xe_spv_sink_workspace {
  xe_spv_sink_entry *entries;
  size_t capacity;
} xe_spv_sink_workspace;
/* Inputs are immutable and cannot alias out. Reuses cold workspace/output
 * capacity. Unsupported/malformed modules leave out empty and return zero.
 * Texture load push constants gain uint atlas_y/layer/pitch_reciprocal at
 * byte offsets 40/44/48. Destination becomes set0 binding1 uimage2DArray;
 * source set1 binding0 and all decode instructions remain unchanged.
 * Caller supplies host_offset=0, a 16-byte-aligned row pitch and exact
 * reciprocal for pitch/16. These owned kernels write aligned 128-bit tuples;
 * atlas rows cover their padded stored-level extents, not just logical mips. */
int xe_spv_image_sink(xe_spv_sink_workspace *, xe_spv_binary *out,
    const uint32_t *words, size_t count, unsigned bytes_per_block);
void xe_spv_sink_close(xe_spv_sink_workspace *);
#ifdef __cplusplus
}
#endif
#endif
