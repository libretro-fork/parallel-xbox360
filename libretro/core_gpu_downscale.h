/* Borrowed storage-buffer views for direct downscale writes. MSVC C89. */
#ifndef XE_CORE_GPU_DOWNSCALE_H
#define XE_CORE_GPU_DOWNSCALE_H
#include <stdint.h>
typedef struct xe_gpu_downscale_view {
   uint64_t source_offset, destination_offset;
   uint32_t source_size, destination_size;
   uint32_t source_bias, destination_bias, tiles;
} xe_gpu_downscale_view;
static int xe_gpu_downscale_plan(uint64_t source, uint64_t destination,
      uint64_t source_capacity, uint64_t destination_capacity,
      uint32_t tile_bytes, uint32_t scale_area, uint32_t remaining_tiles,
      uint32_t alignment, uint32_t range_limit, uint32_t group_limit,
      xe_gpu_downscale_view *view)
{
   uint64_t scaled_tile, source_bytes, destination_bytes, mask, count, limit;
   uint32_t source_bias, destination_bias;
   if (!view || !tile_bytes || !scale_area || !remaining_tiles || !group_limit ||
         alignment < 4 || (alignment & (alignment-1)) ||
         ((source | destination) & 3)) return 0;
   scaled_tile = (uint64_t)tile_bytes * scale_area;
   /* Every later tile-aligned view must make progress too. Check this before
    * any dispatch, avoiding a partially encoded mirror on unsupported limits. */
   if (alignment-4 >= range_limit || scaled_tile > range_limit-(alignment-4) ||
         tile_bytes > range_limit-(alignment-4)) return 0;
   source_bytes = scaled_tile * remaining_tiles;
   destination_bytes = (uint64_t)tile_bytes * remaining_tiles;
   if (source > source_capacity || source_bytes > source_capacity-source ||
         destination > destination_capacity ||
         destination_bytes > destination_capacity-destination) return 0;
   mask = (uint64_t)alignment-1;
   source_bias = (uint32_t)(source & mask);
   destination_bias = (uint32_t)(destination & mask);
   count = (range_limit-source_bias) / scaled_tile;
   limit = (range_limit-destination_bias) / tile_bytes;
   if (count > limit) count = limit;
   if (count > group_limit) count = group_limit;
   if (count > remaining_tiles) count = remaining_tiles;
   if (!count) return 0;
   view->source_offset = source & ~mask;
   view->destination_offset = destination & ~mask;
   view->source_bias = source_bias;
   view->destination_bias = destination_bias;
   view->source_size = (uint32_t)(source_bias + count*scaled_tile);
   view->destination_size = (uint32_t)(destination_bias + count*tile_bytes);
   view->tiles = (uint32_t)count;
   return 1;
}
#endif
