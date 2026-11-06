/* Native-type payload fixture, not an invocation of DeferredCommandBuffer or
 * a Vulkan device. Private packet shapes are mirrored only for storage tests. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <vulkan/vulkan.h>
static size_t allocations, freed;
static void *poison_alloc(size_t n) { void *p = malloc(n); if (p) { memset(p, 0xa5, n); ++allocations; } return p; }
static void poison_free(void *p) { if (p) ++freed; free(p); }
#define XE_COMMAND_STREAM_ALLOC poison_alloc
#define XE_COMMAND_STREAM_FREE poison_free
#include "core_command_stream.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
typedef struct header { uint32_t command, elements; } header;
typedef struct copy_args { VkBuffer src, dst; uint32_t count; } copy_args;
typedef struct render_args {
  VkRenderingFlags flags; VkRect2D area;
  uint32_t layers, views, colors; uint8_t depth, stencil;
} render_args;
typedef struct push_args { VkPipelineLayout layout; VkShaderStageFlags stages; uint32_t offset, size; } push_args;
static size_t align8(size_t n) { return (n + 7) & ~(size_t)7; }
static unsigned char *append(xe_command_stream *s, uint32_t op, size_t arguments) {
  uint32_t elements; size_t bytes; header *h;
  if (!xe_command_packet_layout(arguments, sizeof(*h), &elements, &bytes)) return NULL;
  h = (header *)xe_command_stream_append(s, bytes); if (!h) return NULL;
  h->command = op; h->elements = elements; return (unsigned char *)(h + 1);
}
static int emit(xe_command_stream *s, uint32_t id, uint32_t op) {
  unsigned char *payload;
  uint32_t i, count;
  if (op == 1) {
    copy_args *a; VkBufferCopy *r;
    count = id ? id % 7 + 1 : 65536;
    payload = append(s, op, align8(sizeof(*a)) + count * sizeof(*r)); CHECK(payload);
    a = (copy_args *)payload; a->src = (VkBuffer)(uintptr_t)(0x1000 + id); a->dst = (VkBuffer)(uintptr_t)(0x2000 + id); a->count = count;
    r = (VkBufferCopy *)(payload + align8(sizeof(*a)));
    for (i = 0; i < count; ++i) { r[i].srcOffset = i * 64u; r[i].dstOffset = i * 128u; r[i].size = i + id + 1; }
  } else if (op == 2) {
    render_args *a; VkRenderingAttachmentInfo *r;
    count = id % 5 + ((id >> 1) & 1) + (id & 1);
    payload = append(s, op, align8(sizeof(*a)) + count * sizeof(*r)); CHECK(payload);
    a = (render_args *)payload; a->flags = 0; a->area.offset.x = (int32_t)id; a->area.offset.y = -(int32_t)id;
    a->area.extent.width = id + 1; a->area.extent.height = id + 2;
    a->layers = 1; a->views = 0; a->colors = id % 5; a->depth = (uint8_t)((id >> 1) & 1); a->stencil = (uint8_t)(id & 1);
    r = (VkRenderingAttachmentInfo *)(payload + align8(sizeof(*a)));
    for (i = 0; i < count; ++i) {
      uint32_t j;
      r[i].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO; r[i].pNext = NULL;
      r[i].imageView = (VkImageView)(uintptr_t)(0x3000 + i + id); r[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
      r[i].resolveMode = VK_RESOLVE_MODE_NONE; r[i].resolveImageView = VK_NULL_HANDLE; r[i].resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      r[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; r[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
      for (j = 0; j < 4; ++j) r[i].clearValue.color.uint32[j] = id + i + j;
    }
  } else if (op == 3) {
    push_args *a;
    count = id % 19 + 1;
    payload = append(s, op, sizeof(*a) + count); CHECK(payload);
    a = (push_args *)payload; a->layout = (VkPipelineLayout)(uintptr_t)0x4000;
    a->stages = VK_SHADER_STAGE_VERTEX_BIT; a->offset = 0; a->size = count;
    for (i = 0; i < count; ++i) payload[sizeof(*a) + i] = (unsigned char)(id + i);
  } else { CHECK(append(s, op, 0)); }
  return 0;
}
static int verify(const xe_command_stream *s) {
  const xe_command_page *page;
  uint32_t sequence = 0;
  for (page = s->first; page; page = page->next) {
    size_t at = 0;
    while (at < page->used) {
      const header *h = (const header *)(xe_command_page_const_data(page) + at);
      const unsigned char *payload = (const unsigned char *)(h + 1);
      uint32_t id = sequence ? (sequence - 1) / 4 + 1 : 0;
      uint32_t op = sequence ? (sequence - 1) % 4 + 1 : 1;
      uint32_t i, count;
      CHECK(h->command == op && sizeof(*h) + (size_t)h->elements * 8 <= page->used - at);
      CHECK((uintptr_t)payload % sizeof(uintmax_t) == 0);
      if (op == 1) {
        const copy_args *a = (const copy_args *)payload;
        const VkBufferCopy *r = (const VkBufferCopy *)(payload + align8(sizeof(*a)));
        count = id ? id % 7 + 1 : 65536;
        CHECK(a->src == (VkBuffer)(uintptr_t)(0x1000 + id) && a->dst == (VkBuffer)(uintptr_t)(0x2000 + id) && a->count == count);
        CHECK((size_t)h->elements * 8 == align8(sizeof(*a)) + count * sizeof(*r));
        for (i = 0; i < count; ++i) CHECK(r[i].srcOffset == i * 64u && r[i].dstOffset == i * 128u && r[i].size == i + id + 1);
      } else if (op == 2) {
        const render_args *a = (const render_args *)payload;
        const VkRenderingAttachmentInfo *r = (const VkRenderingAttachmentInfo *)(payload + align8(sizeof(*a)));
        count = id % 5 + ((id >> 1) & 1) + (id & 1);
        CHECK(!a->flags && a->area.offset.x == (int32_t)id && a->area.offset.y == -(int32_t)id);
        CHECK(a->area.extent.width == id + 1 && a->area.extent.height == id + 2 && a->layers == 1 && !a->views);
        CHECK(a->colors == id % 5 && a->depth == ((id >> 1) & 1) && a->stencil == (id & 1));
        CHECK((size_t)h->elements * 8 == align8(sizeof(*a)) + count * sizeof(*r));
        for (i = 0; i < count; ++i) {
          uint32_t j;
          CHECK(r[i].sType == VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO && !r[i].pNext);
          CHECK(r[i].imageView == (VkImageView)(uintptr_t)(0x3000 + i + id) && r[i].imageLayout == VK_IMAGE_LAYOUT_GENERAL);
          CHECK(r[i].resolveMode == VK_RESOLVE_MODE_NONE && r[i].resolveImageView == VK_NULL_HANDLE && r[i].resolveImageLayout == VK_IMAGE_LAYOUT_UNDEFINED);
          CHECK(r[i].loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR && r[i].storeOp == VK_ATTACHMENT_STORE_OP_STORE);
          for (j = 0; j < 4; ++j) CHECK(r[i].clearValue.color.uint32[j] == id + i + j);
        }
      } else if (op == 3) {
        const push_args *a = (const push_args *)payload;
        count = id % 19 + 1;
        CHECK(a->layout == (VkPipelineLayout)(uintptr_t)0x4000 && a->stages == VK_SHADER_STAGE_VERTEX_BIT && !a->offset && a->size == count);
        CHECK((size_t)h->elements * 8 == align8(sizeof(*a) + count));
        for (i = 0; i < count; ++i) CHECK(payload[sizeof(*a) + i] == (unsigned char)(id + i));
      } else CHECK(!h->elements);
      ++sequence; at += sizeof(*h) + (size_t)h->elements * 8;
    }
    CHECK(at == page->used);
  }
  CHECK(sequence == 4097); return 0;
}
int main(void) {
  xe_command_stream s = {0};
  uint32_t repeat, id, op;
  size_t before = 0;
  CHECK(xe_command_stream_init(&s, 1024 * 1024));
  for (repeat = 0; repeat < 2; ++repeat) {
    CHECK(emit(&s, 0, 1) == 0);
    for (id = 1; id <= 1024; ++id) for (op = 1; op <= 4; ++op) CHECK(emit(&s, id, op) == 0);
    CHECK(verify(&s) == 0 && s.size > 1024 * 1024);
    if (repeat) CHECK(allocations == before);
    before = allocations; xe_command_stream_reset(&s);
  }
  xe_command_stream_destroy(&s); CHECK(allocations == freed);
  puts("Vulkan-shaped storage: 8,194 poisoned native-type packets, 65,536-copy payload, optional attachments, push tails and warmed replay pass");
  return 0;
}
