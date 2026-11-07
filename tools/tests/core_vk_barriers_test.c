/* Native barrier ownership, ordering and failure tests, MSVC C89. No GPU. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core_command_rows_profile.h"
#include "core_vk_barriers.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %u: %s\n", (unsigned)__LINE__, #x); exit(1); } } while (0)
typedef struct allocation { void *p; size_t size; } allocation;
static allocation blocks[8192];
static size_t attempts, allocations, live, bytes_live, peak, fail_at;
void *xe_test_command_alloc(size_t size) {
  size_t i;
  void *p;
  ++attempts;
  if (attempts == fail_at) return NULL;
  p = malloc(size); CHECK(p);
  for (i = 0; i < 8192 && blocks[i].p; ++i) {}
  CHECK(i < 8192); blocks[i].p = p; blocks[i].size = size;
  ++allocations; ++live; bytes_live += size;
  if (bytes_live > peak) peak = bytes_live;
  memset(p, 0xa5, size); return p;
}
void xe_test_command_free(void *p) {
  size_t i;
  if (!p) return;
  for (i = 0; i < 8192 && blocks[i].p != p; ++i) {}
  CHECK(i < 8192); --live; bytes_live -= blocks[i].size; blocks[i].p = NULL;
  free(p);
}
static VkBuffer buffer_id(unsigned id) { return (VkBuffer)(uintptr_t)id; }
static VkImage image_id(unsigned id) { return (VkImage)(uintptr_t)id; }
static VkImageSubresourceRange range_make(unsigned aspect, unsigned mip,
    unsigned levels, unsigned layer, unsigned layers) {
  VkImageSubresourceRange r;
  r.aspectMask = aspect; r.baseMipLevel = mip; r.levelCount = levels;
  r.baseArrayLayer = layer; r.layerCount = layers; return r;
}
static xe_vk_barrier_group saved[65536];
static size_t saved_count, reject_call, emit_calls;
static int capture(void *context, const xe_command_row_view *view) {
  const xe_vk_barrier_group *groups = (const xe_vk_barrier_group *)view->data;
  uint32_t i;
  (void)context;
  if (++emit_calls == reject_call) return 0;
  for (i = 0; i < view->count; ++i) {
    CHECK(saved_count < 65536); saved[saved_count++] = groups[i];
  }
  return 1;
}
static void capture_reset(void) { saved_count = emit_calls = reject_call = 0; }
static int push_buffer(xe_vk_barriers *q, unsigned id, unsigned src, unsigned dst) {
  return xe_vk_barriers_buffer(q, buffer_id(id), 64, 128,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
      src, dst, 3, 7, 0);
}
static void failures(void) {
  xe_vk_barriers q;
  size_t i;
  for (i = 1; i <= 3; ++i) {
    memset(&q, 0, sizeof(q)); fail_at = attempts + i;
    CHECK(!xe_vk_barriers_init(&q, 2, 2, 2)); CHECK(!live);
    xe_vk_barriers_close(&q); CHECK(!live);
  }
  fail_at = 0; memset(&q, 0, sizeof(q));
  CHECK(!xe_vk_barriers_init(&q, (size_t)-1, 2, 2));
  CHECK(!xe_vk_barriers_init(&q, 2, 0, 2));
  CHECK(xe_vk_barriers_init(&q, 1, 1, 1));
  CHECK(!xe_vk_barriers_init(&q, 1, 1, 1));
  CHECK(push_buffer(&q, 1, 1, 2) == 1);
  /* Filling the group page is separate from growing native row storage. */
  fail_at = attempts + 1;
  CHECK(push_buffer(&q, 1, 2, 1) == -1 && q.groups.pending == 1);
  fail_at = 0; CHECK(push_buffer(&q, 1, 2, 1) == 1);
  fail_at = attempts + 1;
  CHECK(push_buffer(&q, 1, 1, 2) == -1 && q.current.buffer_count == 1);
  fail_at = 0; CHECK(push_buffer(&q, 1, 1, 2) == 1);
  capture_reset(); reject_call = 2;
  CHECK(!xe_vk_barriers_submit(&q, capture, NULL));
  CHECK(saved_count == 1 && q.groups.pending == 2);
  reject_call = 0; CHECK(xe_vk_barriers_submit(&q, capture, NULL));
  CHECK(saved_count == 3 && !xe_vk_barriers_pending(&q));
  CHECK(saved[0].buffers->srcAccessMask == 1 && saved[1].buffers->srcAccessMask == 2 &&
      saved[2].buffers->srcAccessMask == 1);
  CHECK(xe_vk_barriers_reset(&q));
  CHECK(push_buffer(&q, 1, 1, 2) == 1);
  CHECK(!xe_vk_barriers_reset(&q) && q.current.buffers->srcAccessMask == 1);
  xe_vk_barriers_close(&q); CHECK(!live);
}
static void cycles_and_ranges(void) {
  xe_vk_barriers q;
  const VkBufferMemoryBarrier *original;
  VkImageSubresourceRange r;
  size_t i;
  memset(&q, 0, sizeof(q)); CHECK(xe_vk_barriers_init(&q, 8, 8, 8));
  CHECK(push_buffer(&q, 1, 1, 2) == 1); original = q.current.buffers;
  CHECK(xe_vk_barriers_buffer(&q, buffer_id(1), 64, 128, 1, 2, 1, 2, 3, 7, 0) == 1);
  CHECK(q.current.buffers == original && q.current.buffer_count == 1);
  CHECK(q.current.src_stage_mask == (VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | 1));
  CHECK(push_buffer(&q, 1, 2, 1) == 1 && push_buffer(&q, 1, 1, 2) == 1);
  capture_reset(); CHECK(xe_vk_barriers_submit(&q, capture, NULL));
  CHECK(saved_count == 3 && saved[0].buffers == original);
  for (i = 0; i < 3; ++i) CHECK(saved[i].buffer_count == 1);
  CHECK(xe_vk_barriers_reset(&q));
  /* A historical overlap must not split a current independent range. */
  CHECK(xe_vk_barriers_buffer(&q, buffer_id(1), 0, 256, 1, 2, 1, 2, 3, 7, 0) == 1);
  CHECK(xe_vk_barriers_buffer(&q, buffer_id(1), 0, 128, 1, 2, 2, 1, 3, 7, 0) == 1);
  CHECK(xe_vk_barriers_buffer(&q, buffer_id(1), 128, 128, 1, 2, 2, 1, 3, 7, 0) == 1);
  CHECK(q.groups.pending == 1 && q.current.buffer_count == 2);
  capture_reset(); CHECK(xe_vk_barriers_submit(&q, capture, NULL)); CHECK(saved_count == 2);
  CHECK(xe_vk_barriers_reset(&q));
  r = range_make(VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1);
  for (i = 0; i < 3; ++i) CHECK(xe_vk_barriers_image(&q, image_id(1), &r, 0, 0, 0, 0,
      i & 1 ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL,
      i & 1 ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 3, 7, 0) == 1);
  capture_reset(); CHECK(xe_vk_barriers_submit(&q, capture, NULL)); CHECK(saved_count == 3);
  for (i = 0; i < 3; ++i) CHECK(saved[i].image_count == 1 && !saved[i].src_stage_mask);
  CHECK(xe_vk_barriers_reset(&q));
  /* Distinct aspects, mip intervals and array layers can share a group. */
  r = range_make(VK_IMAGE_ASPECT_DEPTH_BIT, 0, 2, 0, 2);
  CHECK(xe_vk_barriers_image(&q, image_id(3), &r, 1, 2, 1, 2, VK_IMAGE_LAYOUT_GENERAL,
      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 3, 7, 0) == 1);
  r.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
  CHECK(xe_vk_barriers_image(&q, image_id(3), &r, 1, 2, 1, 2, VK_IMAGE_LAYOUT_GENERAL,
      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 3, 7, 0) == 1);
  r.baseMipLevel = 2; r.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
  CHECK(xe_vk_barriers_image(&q, image_id(3), &r, 1, 2, 1, 2, VK_IMAGE_LAYOUT_GENERAL,
      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 3, 7, 0) == 1);
  r.baseMipLevel = 0; r.baseArrayLayer = 2;
  CHECK(xe_vk_barriers_image(&q, image_id(3), &r, 1, 2, 1, 2, VK_IMAGE_LAYOUT_GENERAL,
      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 3, 7, 0) == 1);
  CHECK(q.current.image_count == 4 && !q.groups.pending);
  r.baseArrayLayer = 1; r.layerCount = VK_REMAINING_ARRAY_LAYERS;
  CHECK(xe_vk_barriers_image(&q, image_id(3), &r, 1, 2, 2, 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      VK_IMAGE_LAYOUT_GENERAL, 7, 3, 0) == 1 && q.groups.pending == 1);
  capture_reset(); CHECK(xe_vk_barriers_submit(&q, capture, NULL)); CHECK(saved_count == 2);
  CHECK(xe_vk_barriers_reset(&q));
  CHECK(xe_vk_barriers_buffer(&q, buffer_id(1), 0, 1, 1, 1, 2, 2, 3, 3, 1) == 0);
  CHECK(xe_vk_barriers_image(&q, image_id(3), &r, 1, 1, 2, 2, VK_IMAGE_LAYOUT_GENERAL,
      VK_IMAGE_LAYOUT_GENERAL, 3, 3, 1) == 0 && !xe_vk_barriers_pending(&q));
  xe_vk_barriers_close(&q); CHECK(!live);
}
static uint32_t random_state = 0x12345678;
static uint32_t next_random(void) { random_state = random_state * 1664525u + 1013904223u; return random_state; }
static void overlap_reference(void) {
  unsigned i;
  for (i = 0; i < 1000000; ++i) {
    unsigned a = next_random() % 100, b = next_random() % 100;
    unsigned n = next_random() % 50 + 1, m = next_random() % 50 + 1;
    unsigned k, overlap = 0;
    for (k = 0; k < 150; ++k) if (k >= a && k - a < n && k >= b && k - b < m) overlap = 1;
    CHECK(xe_vk_barrier_overlap(a, n, b, m, VK_WHOLE_SIZE) == (int)overlap);
  }
  CHECK(xe_vk_barrier_overlap(VK_WHOLE_SIZE - 8, 16, VK_WHOLE_SIZE - 2, 1, VK_WHOLE_SIZE));
  CHECK(xe_vk_barrier_overlap(5, VK_WHOLE_SIZE, 900, 1, VK_WHOLE_SIZE));
  CHECK(!xe_vk_barrier_overlap(5, VK_WHOLE_SIZE, 0, 5, VK_WHOLE_SIZE));
  CHECK(xe_vk_barrier_overlap(4, VK_REMAINING_MIP_LEVELS, 100, 1, VK_REMAINING_MIP_LEVELS));
}
static void stable_pages(void) {
  xe_vk_barriers q;
  VkImageSubresourceRange r = range_make(1, 0, 1, 0, 1);
  size_t cycle, i, before = 0;
  memset(&q, 0, sizeof(q)); CHECK(xe_vk_barriers_init(&q, 7, 5, 3));
  for (cycle = 0; cycle < 20; ++cycle) {
    size_t buffers = 0, images = 0;
    capture_reset();
    for (i = 0; i < 10000; ++i) {
      CHECK(push_buffer(&q, (unsigned)(i % 13 + 1), (unsigned)i + 1, (unsigned)i + 2) == 1);
      CHECK(xe_vk_barriers_image(&q, image_id((unsigned)(i % 11 + 1)), &r, 1, 2,
          (unsigned)i + 1, (unsigned)i + 2, VK_IMAGE_LAYOUT_GENERAL,
          VK_IMAGE_LAYOUT_GENERAL, 3, 7, 0) == 1);
      if (i % 97 == 96) CHECK(xe_vk_barriers_submit(&q, capture, NULL));
    }
    CHECK(xe_vk_barriers_submit(&q, capture, NULL));
    /* Inspect after every producer append and publication has finished. */
    for (i = 0; i < saved_count; ++i) {
      const xe_vk_barrier_group *g = saved + i;
      uint32_t j;
      for (j = 0; j < g->buffer_count; ++j) {
        const VkBufferMemoryBarrier *b = g->buffers + j;
        CHECK(b->sType == VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER && !b->pNext);
        CHECK(b->buffer == buffer_id((unsigned)(buffers % 13 + 1)) && b->offset == 64 && b->size == 128);
        CHECK(b->srcAccessMask == buffers + 1 && b->dstAccessMask == buffers + 2);
        CHECK(b->srcQueueFamilyIndex == 3 && b->dstQueueFamilyIndex == 7); ++buffers;
      }
      for (j = 0; j < g->image_count; ++j) {
        const VkImageMemoryBarrier *b = g->images + j;
        CHECK(b->sType == VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER && !b->pNext);
        CHECK(b->image == image_id((unsigned)(images % 11 + 1)));
        CHECK(b->srcAccessMask == images + 1 && b->dstAccessMask == images + 2);
        CHECK(b->oldLayout == VK_IMAGE_LAYOUT_GENERAL && b->newLayout == VK_IMAGE_LAYOUT_GENERAL);
        CHECK(b->subresourceRange.aspectMask == 1 && b->subresourceRange.levelCount == 1 &&
            b->subresourceRange.layerCount == 1 && !b->subresourceRange.baseMipLevel &&
            !b->subresourceRange.baseArrayLayer);
        CHECK(b->srcQueueFamilyIndex == 3 && b->dstQueueFamilyIndex == 7); ++images;
      }
    }
    CHECK(buffers == 10000 && images == 10000);
    if (!cycle) before = allocations; else CHECK(allocations == before);
    CHECK(xe_vk_barriers_reset(&q));
  }
  xe_vk_barriers_close(&q); CHECK(!live);
}
#ifdef XE_VK_REPLAY_TEST
extern void *xe_test_vk_new(size_t);
extern int xe_test_vk_try_new(void);
extern void xe_test_vk_delete(void *);
extern void xe_test_vk_buffer(void *, unsigned);
extern void xe_test_vk_image(void *, unsigned);
extern int xe_test_vk_submit(void *);
extern void xe_test_vk_marker(void *, unsigned);
extern void xe_test_vk_copy(void *, const VkMemoryBarrier *, const VkBufferMemoryBarrier *, const VkImageMemoryBarrier *);
extern void xe_test_vk_execute(void *, PFN_vkCmdPipelineBarrier, PFN_vkCmdSetStencilReference);
extern void xe_test_vk_reset(void *);
static unsigned replay_buffers, replay_images, replay_calls, marker_seen, marker_expected, external_mode, cycle_mode;
static int native_page(const void *ptr, size_t extent, size_t capacity) {
  size_t i;
  uintptr_t address = (uintptr_t)ptr;
  for (i = 0; i < 8192; ++i) if (blocks[i].p && blocks[i].size == xe_command_page_header_size() + capacity) {
    uintptr_t base = (uintptr_t)blocks[i].p + xe_command_page_header_size();
    if (address >= base && address - base <= capacity && extent <= capacity - (address - base)) return 1;
  }
  return 0;
}
static void VKAPI_CALL replay_barrier(VkCommandBuffer cb, VkPipelineStageFlags src,
    VkPipelineStageFlags dst, VkDependencyFlags flags, uint32_t nm, const VkMemoryBarrier *m,
    uint32_t nb, const VkBufferMemoryBarrier *b, uint32_t ni, const VkImageMemoryBarrier *im) {
  uint32_t j;
  CHECK((uintptr_t)cb == 123 && marker_seen == marker_expected);
  ++replay_calls;
  if (external_mode) {
    CHECK(src == 1 && dst == 2 && flags == VK_DEPENDENCY_BY_REGION_BIT && nm == 1 && nb == 1 && ni == 1);
    CHECK(m->srcAccessMask == 4 && m->dstAccessMask == 8 && !m->pNext);
    CHECK(b->buffer == buffer_id(99) && b->size == 256 && b->offset == 512 && !b->pNext);
    CHECK(im->image == image_id(77) && im->newLayout == VK_IMAGE_LAYOUT_GENERAL && !im->pNext);
    return;
  }
  CHECK(src == VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT && dst == VK_PIPELINE_STAGE_TRANSFER_BIT && !flags && !nm);
  CHECK(!m);
  if (nb) CHECK(native_page(b, sizeof(*b) * nb, 256 * sizeof(*b)));
  if (ni) CHECK(native_page(im, sizeof(*im) * ni, 128 * sizeof(*im)));
  for (j = 0; j < nb; ++j) {
    unsigned n = cycle_mode && replay_buffers == 2 ? 0 : replay_buffers;
    CHECK(b[j].buffer == buffer_id(1) && b[j].srcAccessMask == n + 1 && b[j].dstAccessMask == n + 2);
    CHECK(b[j].offset == 64 && b[j].size == 128 && b[j].srcQueueFamilyIndex == 3 && b[j].dstQueueFamilyIndex == 7);
    CHECK(b[j].sType == VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER && !b[j].pNext); ++replay_buffers;
  }
  for (j = 0; j < ni; ++j) {
    unsigned n = cycle_mode && replay_images == 2 ? 0 : replay_images;
    CHECK(im[j].image == image_id(1) && im[j].srcAccessMask == n + 1 && im[j].dstAccessMask == n + 2);
    CHECK(im[j].oldLayout == (n & 1 ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL));
    CHECK(im[j].newLayout == (n & 1 ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL));
    CHECK(im[j].sType == VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER && !im[j].pNext);
    CHECK(im[j].srcQueueFamilyIndex == 3 && im[j].dstQueueFamilyIndex == 7);
    CHECK(im[j].subresourceRange.aspectMask == 1 && !im[j].subresourceRange.baseMipLevel &&
        im[j].subresourceRange.levelCount == 1 && !im[j].subresourceRange.baseArrayLayer && im[j].subresourceRange.layerCount == 1);
    ++replay_images;
  }
}
static void VKAPI_CALL replay_marker(VkCommandBuffer cb, VkStencilFaceFlags face, uint32_t value) {
  CHECK((uintptr_t)cb == 123 && face == VK_STENCIL_FACE_FRONT_AND_BACK); marker_seen = value;
}
static void replay(void) {
  void *p;
  unsigned i, cycle;
  size_t before = 0;
  VkMemoryBarrier m;
  VkBufferMemoryBarrier b;
  VkImageMemoryBarrier im;
  for (i = 1; i <= 4; ++i) {
    fail_at = attempts + i; CHECK(!xe_test_vk_try_new() && !live);
  }
  fail_at = 0; p = xe_test_vk_new(4096);
  for (i = 0; i < 3; ++i) { xe_test_vk_buffer(p, i & 1); xe_test_vk_image(p, i & 1); }
  CHECK(xe_test_vk_submit(p)); cycle_mode = 1;
  xe_test_vk_execute(p, replay_barrier, replay_marker);
  CHECK(replay_buffers == 3 && replay_images == 3); cycle_mode = 0;
  xe_test_vk_reset(p);
  for (cycle = 0; cycle < 20; ++cycle) {
    replay_buffers = replay_images = marker_seen = marker_expected = 0;
    for (i = 0; i < 10000; ++i) { xe_test_vk_buffer(p, i); xe_test_vk_image(p, i); }
    xe_test_vk_marker(p, 91); marker_expected = 91;
    CHECK(xe_test_vk_submit(p));
    xe_test_vk_execute(p, replay_barrier, replay_marker);
    CHECK(replay_buffers == 10000 && replay_images == 10000);
    if (!cycle) before = allocations; else CHECK(before == allocations);
    xe_test_vk_reset(p);
  }
  /* Already pending records survive starting the next command submission. */
  xe_test_vk_buffer(p, 0); xe_test_vk_reset(p); CHECK(xe_test_vk_submit(p));
  replay_buffers = replay_images = marker_expected = marker_seen = 0;
  xe_test_vk_execute(p, replay_barrier, replay_marker); CHECK(replay_buffers == 1);
  xe_test_vk_reset(p);
  memset(&m, 0, sizeof(m)); m.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER; m.srcAccessMask = 4; m.dstAccessMask = 8;
  memset(&b, 0, sizeof(b)); b.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER; b.buffer = buffer_id(99); b.size = 256; b.offset = 512;
  memset(&im, 0, sizeof(im)); im.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER; im.image = image_id(77); im.newLayout = VK_IMAGE_LAYOUT_GENERAL;
  xe_test_vk_copy(p, &m, &b, &im);
  memset(&m, 0, sizeof(m)); memset(&b, 0, sizeof(b)); memset(&im, 0, sizeof(im));
  external_mode = 1; xe_test_vk_execute(p, replay_barrier, replay_marker); external_mode = 0;
  xe_test_vk_delete(p); CHECK(!live);
  /* Fail publication after the first 256-group span has been recorded. */
  p = xe_test_vk_new(24);
  for (i = 0; i < 258; ++i) xe_test_vk_buffer(p, i);
  fail_at = attempts + 1; CHECK(!xe_test_vk_submit(p));
  fail_at = 0; CHECK(xe_test_vk_submit(p));
  replay_buffers = replay_images = marker_expected = marker_seen = 0;
  xe_test_vk_execute(p, replay_barrier, replay_marker); CHECK(replay_buffers == 258);
  xe_test_vk_delete(p); CHECK(!live);
  printf("actual Vulkan CPU replay: 400265 rows, %u calls, 19 warmed cycles without storage allocation\n", replay_calls);
}
#endif
int main(void) {
  failures(); cycles_and_ranges(); overlap_reference(); stable_pages();
#ifdef XE_VK_REPLAY_TEST
  replay();
#endif
  CHECK(!live && !bytes_live);
  printf("Vulkan C89 barriers: 400000 retained rows, 1000000 range checks, balanced teardown; peak %lu bytes\n", (unsigned long)peak);
  return 0;
}
