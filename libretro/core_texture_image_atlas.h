/* Stored-level image atlas for direct texture decode. Checked C89, no heap. */
#ifndef XE_CORE_TEXTURE_IMAGE_ATLAS_H
#define XE_CORE_TEXTURE_IMAGE_ATLAS_H
#include "core_gpu_divide.h"
typedef struct xe_texture_image_atlas { uint32_t width, height; } xe_texture_image_atlas;
typedef struct xe_texture_image_level {
  uint32_t width, height, y, reciprocal;
} xe_texture_image_level;
typedef struct xe_texture_image_push { uint32_t y, layer, reciprocal; } xe_texture_image_push;
typedef struct xe_texture_load_layout {
  uint64_t offset_bytes, slice_size_bytes;
  uint32_t x_pitch_blocks, y_pitch_blocks;
  xe_texture_image_level image;
} xe_texture_load_layout;
typedef struct xe_texture_blit_level {
  uint32_t level, offset_blocks, pitch_blocks, slice_blocks, dest_offset_y;
  uint32_t size_blocks_x, size_blocks_y, atlas_x, atlas_y;
  uint64_t image_offset_bytes;
} xe_texture_blit_level;
typedef struct xe_texture_image_plan {
  xe_texture_image_atlas atlas;
  uint32_t image_count, image_loop_end, buffer_first;
  uint64_t buffer_origin, buffer_bytes;
} xe_texture_image_plan;
static INLINE int xe_texture_image_add(xe_texture_image_atlas *atlas,
    uint32_t width, uint32_t height, unsigned bytes, uint32_t limit,
    xe_texture_image_level *out) {
  uint64_t pitch = (uint64_t)width * bytes;
  xe_texture_image_level level;
  if (!width || !height || (bytes != 4 && bytes != 8 && bytes != 16) ||
      width > limit || height > limit || atlas->height > limit - height ||
      (pitch & 15) || pitch > UINT32_MAX) return 0;
  level.width = width; level.height = height; level.y = atlas->height;
  if (!xe_gpu_divide_prepare((uint32_t)(pitch >> 4), &level.reciprocal)) return 0;
  if (width > atlas->width) atlas->width = width;
  atlas->height += height; *out = level; return 1;
}
/* Copy only complete host blocks. Offsets borrow the existing stored-level
 * layout; packed mip tails are not decoded again or flattened into a buffer. */
static INLINE int xe_texture_image_origin(const xe_texture_image_level *level,
    uint64_t byte_offset, unsigned bytes, uint32_t width, uint32_t height,
    uint32_t *x, uint32_t *y) {
  uint64_t blocks, row, column;
  if (!bytes || byte_offset % bytes || !level->width || !width || !height) return 0;
  blocks = byte_offset / bytes;
  row = blocks / level->width; column = blocks % level->width;
  if (column >= level->width || width > level->width - column ||
      row >= level->height || height > level->height - row ||
      row > UINT32_MAX - level->y) return 0;
  *x = (uint32_t)column; *y = level->y + (uint32_t)row; return 1;
}
/* Borrow the original stored layouts. Packed level zero has separate base and
 * mip storage; loop 1 addresses mips[0], not the base's copy of that tail. */
static INLINE xe_texture_load_layout *xe_texture_stored_layout(
    xe_texture_load_layout *base, xe_texture_load_layout *mips,
    uint32_t packed, uint32_t loop) {
  return !loop ? base : &mips[packed ? loop : 0];
}
/* All partial-copy descendants sharing a packed tail stay in the buffer. Each
 * stored level is decoded exactly once. On failure plan is unchanged; image
 * fields in the borrowed scratch layouts/copies may have been prepared and
 * must only be consumed on success. No allocation or extra payload copy. */
static INLINE int xe_texture_image_plan_prepare(xe_texture_image_plan *out,
    xe_texture_load_layout *base, xe_texture_load_layout *mips, uint32_t capacity,
    uint32_t first, uint32_t last, uint32_t packed, uint32_t complete_count,
    xe_texture_blit_level *copies, uint32_t layers, unsigned bytes,
    uint32_t limit, uint64_t total_bytes) {
  xe_texture_image_plan plan;
  xe_texture_load_layout *layout;
  uint32_t end, loop_first, loop_last, loop, i;
  uint64_t cursor = 0, size;
  if (!out || !base || !mips || !copies || !capacity || first > last ||
      last >= capacity || (packed >= capacity && packed != UINT32_MAX) || !layers ||
      complete_count > last - first + 1) return 0;
  end = first + complete_count;
  if (end <= last) {
    /* An incomplete logical level excludes its entire stored tail. */
    if (packed) { if (end > packed) end = packed; }
    else if (end > 1) end = 1;
    if (end < first) end = first;
  }
  if (end == first) return 0;
  loop_first = packed ? (first < packed ? first : packed) : (first ? 1 : 0);
  loop_last = packed ? (last < packed ? last : packed) : (last ? 1 : 0);
  plan.image_count = end - first; plan.buffer_first = end;
  if (end > last) plan.image_loop_end = loop_last + 1;
  else plan.image_loop_end = packed ? end : 1;
  plan.atlas.width = plan.atlas.height = 0;
  plan.buffer_origin = total_bytes; plan.buffer_bytes = 0;
  for (loop = loop_first; loop <= loop_last; ++loop) {
    layout = xe_texture_stored_layout(base,mips,packed,loop);
    if (layout->offset_bytes != cursor || !layout->slice_size_bytes ||
        layout->slice_size_bytes > (UINT64_MAX - cursor) / layers) return 0;
    size = layout->slice_size_bytes * layers;
    if (loop < plan.image_loop_end) {
      if (!xe_texture_image_add(&plan.atlas,layout->x_pitch_blocks,
          layout->y_pitch_blocks,bytes,limit,&layout->image) ||
          (uint64_t)layout->x_pitch_blocks * bytes * layout->y_pitch_blocks !=
          layout->slice_size_bytes) return 0;
    } else if (plan.buffer_origin == total_bytes) plan.buffer_origin = cursor;
    cursor += size;
  }
  if (cursor != total_bytes || plan.buffer_origin > total_bytes) return 0;
  plan.buffer_bytes = total_bytes - plan.buffer_origin;
  /* The load shader's host byte offset is uint32, including array slices. */
  if (plan.buffer_bytes > UINT32_MAX) return 0;
  for (i = 0; i < plan.image_count; ++i) {
    if (copies[i].level != first + i) return 0;
    layout = !copies[i].level ? base : &mips[copies[i].level < packed ? copies[i].level : packed];
    if (!xe_texture_image_origin(&layout->image,copies[i].image_offset_bytes,
        bytes,copies[i].size_blocks_x,copies[i].size_blocks_y,
        &copies[i].atlas_x,&copies[i].atlas_y)) return 0;
  }
  *out = plan; return 1;
}
#endif
