/* GPU-owner Vulkan barriers. Native arrays stay in retained pages until CPU
 * command replay finishes. No locks, relocation or intermediate array copies. */
#ifndef XE_CORE_VK_BARRIERS_H
#define XE_CORE_VK_BARRIERS_H
#include <string.h>
#include <vulkan/vulkan_core.h>
#include "core_command_rows.h"

typedef struct xe_vk_barrier_group {
  VkPipelineStageFlags src_stage_mask, dst_stage_mask;
  const VkBufferMemoryBarrier *buffers;
  const VkImageMemoryBarrier *images;
  uint32_t buffer_count, image_count;
} xe_vk_barrier_group;
typedef struct xe_vk_barriers {
  xe_command_stream buffers, images;
  xe_command_rows groups;
  xe_vk_barrier_group current;
} xe_vk_barriers;

static INLINE void xe_vk_barriers_close(xe_vk_barriers *q) {
  xe_command_stream_destroy(&q->buffers);
  xe_command_stream_destroy(&q->images);
  xe_command_rows_close(&q->groups);
  memset(&q->current, 0, sizeof(q->current));
}
/* Zero-initialize first. Failed cold setup releases every partial allocation. */
static INLINE int xe_vk_barriers_init(xe_vk_barriers *q, size_t buffers,
    size_t images, size_t groups) {
  if (q->buffers.first || q->images.first || q->groups.stream.first ||
      !buffers || !images || !groups ||
      sizeof(VkBufferMemoryBarrier) % sizeof(uintmax_t) ||
      sizeof(VkImageMemoryBarrier) % sizeof(uintmax_t) ||
      buffers > (size_t)-1 / sizeof(VkBufferMemoryBarrier) ||
      images > (size_t)-1 / sizeof(VkImageMemoryBarrier) ||
      groups > (size_t)-1 / sizeof(xe_vk_barrier_group)) return 0;
  if (!xe_command_stream_init(&q->buffers, buffers * sizeof(VkBufferMemoryBarrier)) ||
      !xe_command_stream_init(&q->images, images * sizeof(VkImageMemoryBarrier)) ||
      !xe_command_rows_init(&q->groups, sizeof(xe_vk_barrier_group),
          groups * sizeof(xe_vk_barrier_group))) {
    xe_vk_barriers_close(q); return 0;
  }
  return 1;
}
static INLINE int xe_vk_barriers_pending(const xe_vk_barriers *q) {
  return q->groups.pending || q->current.buffer_count || q->current.image_count;
}
static INLINE int xe_vk_barriers_split(xe_vk_barriers *q) {
  xe_vk_barrier_group *group;
  if (!q->current.buffer_count && !q->current.image_count) return 1;
  group = (xe_vk_barrier_group *)xe_command_rows_append(&q->groups);
  if (!group) return 0;
  *group = q->current;
  memset(&q->current, 0, sizeof(q->current));
  return 1;
}
/* Subtraction avoids overflowing end addresses, including WHOLE_SIZE and the
 * REMAINING_* sentinels. Inputs are native Vulkan half-open ranges. */
static INLINE int xe_vk_barrier_overlap(VkDeviceSize a, VkDeviceSize n,
    VkDeviceSize b, VkDeviceSize m, VkDeviceSize remaining) {
  if (a <= b) return n == remaining || b - a < n;
  return m == remaining || a - b < m;
}
/* A group must have contiguous native arrays. At a storage-page boundary,
 * close the group rather than relocating its records. Groups contain no
 * unequal overlapping ranges, so this adds only a conservative dependency. */
static INLINE int xe_vk_barriers_room(xe_vk_barriers *q,
    const xe_command_stream *stream, size_t stride, uint32_t count) {
  if (!stream->current) return 0;
  if (count == UINT32_MAX ||
      stream->current->capacity - stream->current->used < stride)
    return xe_vk_barriers_split(q);
  return 1;
}
/* Return -1 on allocation failure, 0 for skip_if_equal, 1 when queued/reused. */
static INLINE int xe_vk_barriers_buffer(xe_vk_barriers *q, VkBuffer buffer,
    VkDeviceSize offset, VkDeviceSize size, VkPipelineStageFlags src_stage,
    VkPipelineStageFlags dst_stage, VkAccessFlags src_access,
    VkAccessFlags dst_access, uint32_t src_queue, uint32_t dst_queue,
    int skip_if_equal) {
  uint32_t i;
  VkBufferMemoryBarrier *row;
  if (skip_if_equal && src_stage == dst_stage && src_access == dst_access &&
      src_queue == dst_queue) return 0;
  /* Only the current group may absorb duplicates. A prior group may have
   * been followed by an intervening transition that requires this one again. */
  for (i = 0; i < q->current.buffer_count; ++i) {
    const VkBufferMemoryBarrier *other = q->current.buffers + i;
    if (other->buffer != buffer || !xe_vk_barrier_overlap(offset, size,
        other->offset, other->size, VK_WHOLE_SIZE)) continue;
    if (other->offset == offset && other->size == size &&
        other->srcAccessMask == src_access && other->dstAccessMask == dst_access &&
        other->srcQueueFamilyIndex == src_queue && other->dstQueueFamilyIndex == dst_queue) {
      q->current.src_stage_mask |= src_stage;
      q->current.dst_stage_mask |= dst_stage; return 1;
    }
    if (!xe_vk_barriers_split(q)) return -1;
    break;
  }
  if (!xe_vk_barriers_room(q, &q->buffers, sizeof(*row), q->current.buffer_count)) return -1;
  row = (VkBufferMemoryBarrier *)xe_command_stream_append(&q->buffers, sizeof(*row));
  if (!row) return -1;
  row->sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER; row->pNext = NULL;
  row->srcAccessMask = src_access; row->dstAccessMask = dst_access;
  row->srcQueueFamilyIndex = src_queue; row->dstQueueFamilyIndex = dst_queue;
  row->buffer = buffer; row->offset = offset; row->size = size;
  if (!q->current.buffer_count) q->current.buffers = row;
  ++q->current.buffer_count;
  q->current.src_stage_mask |= src_stage; q->current.dst_stage_mask |= dst_stage;
  return 1;
}
static INLINE int xe_vk_barriers_image(xe_vk_barriers *q, VkImage image,
    const VkImageSubresourceRange *range, VkPipelineStageFlags src_stage,
    VkPipelineStageFlags dst_stage, VkAccessFlags src_access,
    VkAccessFlags dst_access, VkImageLayout old_layout, VkImageLayout new_layout,
    uint32_t src_queue, uint32_t dst_queue, int skip_if_equal) {
  uint32_t i;
  VkImageMemoryBarrier *row;
  if (skip_if_equal && src_stage == dst_stage && src_access == dst_access &&
      old_layout == new_layout && src_queue == dst_queue) return 0;
  for (i = 0; i < q->current.image_count; ++i) {
    const VkImageMemoryBarrier *other = q->current.images + i;
    const VkImageSubresourceRange *r = &other->subresourceRange;
    if (other->image != image || !(r->aspectMask & range->aspectMask) ||
        !xe_vk_barrier_overlap(range->baseMipLevel, range->levelCount,
            r->baseMipLevel, r->levelCount, VK_REMAINING_MIP_LEVELS) ||
        !xe_vk_barrier_overlap(range->baseArrayLayer, range->layerCount,
            r->baseArrayLayer, r->layerCount, VK_REMAINING_ARRAY_LAYERS)) continue;
    if (r->aspectMask == range->aspectMask && r->baseMipLevel == range->baseMipLevel &&
        r->levelCount == range->levelCount && r->baseArrayLayer == range->baseArrayLayer &&
        r->layerCount == range->layerCount && other->srcAccessMask == src_access &&
        other->dstAccessMask == dst_access && other->oldLayout == old_layout &&
        other->newLayout == new_layout && other->srcQueueFamilyIndex == src_queue &&
        other->dstQueueFamilyIndex == dst_queue) {
      q->current.src_stage_mask |= src_stage;
      q->current.dst_stage_mask |= dst_stage; return 1;
    }
    if (!xe_vk_barriers_split(q)) return -1;
    break;
  }
  if (!xe_vk_barriers_room(q, &q->images, sizeof(*row), q->current.image_count)) return -1;
  row = (VkImageMemoryBarrier *)xe_command_stream_append(&q->images, sizeof(*row));
  if (!row) return -1;
  row->sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER; row->pNext = NULL;
  row->srcAccessMask = src_access; row->dstAccessMask = dst_access;
  row->oldLayout = old_layout; row->newLayout = new_layout;
  row->srcQueueFamilyIndex = src_queue; row->dstQueueFamilyIndex = dst_queue;
  row->image = image; row->subresourceRange = *range;
  if (!q->current.image_count) q->current.images = row;
  ++q->current.image_count;
  q->current.src_stage_mask |= src_stage; q->current.dst_stage_mask |= dst_stage;
  return 1;
}
static INLINE int xe_vk_barriers_submit(xe_vk_barriers *q,
    xe_command_row_emit_fn emit, void *context) {
  return xe_vk_barriers_split(q) &&
      xe_command_rows_submit(&q->groups, UINT32_MAX, emit, context);
}
/* CPU replay/discard must precede reset. Preserve out-of-submission pending
 * records and every page they may share with already published groups. */
static INLINE int xe_vk_barriers_reset(xe_vk_barriers *q) {
  if (xe_vk_barriers_pending(q)) return 0;
  xe_command_stream_reset(&q->buffers); xe_command_stream_reset(&q->images);
  return xe_command_rows_reset(&q->groups);
}
static INLINE void xe_vk_barrier_groups_execute(PFN_vkCmdPipelineBarrier emit,
    VkCommandBuffer command_buffer, const xe_vk_barrier_group *groups, uint32_t count) {
  uint32_t i;
  for (i = 0; i < count; ++i) {
    const xe_vk_barrier_group *g = groups + i;
    emit(command_buffer,
        g->src_stage_mask ? g->src_stage_mask : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        g->dst_stage_mask ? g->dst_stage_mask : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0, 0, NULL, g->buffer_count, g->buffers, g->image_count, g->images);
  }
}
#ifdef __cplusplus
struct xe_vk_barriers_owner : xe_vk_barriers {
  xe_vk_barriers_owner() : xe_vk_barriers{} {}
  xe_vk_barriers_owner(const xe_vk_barriers_owner&) = delete;
  xe_vk_barriers_owner& operator=(const xe_vk_barriers_owner&) = delete;
  ~xe_vk_barriers_owner() { xe_vk_barriers_close(this); }
};
#endif
#endif
