/* Stored-level image atlas for direct texture decode. Checked C89, no heap. */
#ifndef XE_CORE_TEXTURE_IMAGE_ATLAS_H
#define XE_CORE_TEXTURE_IMAGE_ATLAS_H
#include "core_gpu_divide.h"
typedef struct xe_texture_image_atlas { uint32_t width, height; } xe_texture_image_atlas;
typedef struct xe_texture_image_level {
  uint32_t width, height, y, reciprocal;
} xe_texture_image_level;
typedef struct xe_texture_image_push { uint32_t y, layer, reciprocal; } xe_texture_image_push;
static INLINE int xe_texture_image_add(xe_texture_image_atlas *atlas,
    uint32_t width, uint32_t height, unsigned bytes, uint32_t limit,
    xe_texture_image_level *out) {
  uint64_t pitch = (uint64_t)width * bytes;
  xe_texture_image_level level;
  if (!width || !height || (bytes != 4 && bytes != 8 && bytes != 16) ||
      width > limit || height > limit || atlas->height > limit - height ||
      (pitch & 15) || (pitch >> 4) > UINT32_MAX) return 0;
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
#endif
