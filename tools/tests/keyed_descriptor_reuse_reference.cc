/* Reference only: old global FIFO deque and per-layout free vectors. */
#include <cstdint>
#include <cstdio>
#include <deque>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan_core.h>
#include "core_keyed_descriptor_reuse.h"
struct Used { uint64_t frame; uint32_t key; VkDescriptorSet set; };
int main() {
  xe_keyed_descriptor_reuse pool{};
  std::deque<Used> used;
  std::unordered_map<uint32_t, std::vector<VkDescriptorSet>> free;
  uint64_t old_id = UINT64_C(1) << 40, new_id = old_id;
  uint32_t random = 9771;
  unsigned exceptions = 0;
  if (!xe_keyed_reuse_warm(&pool)) return 1;
  for (uint64_t frame = 1; frame <= 10000; ++frame) {
    uint64_t completed = frame > 3 ? frame - 3 : 0;
    while (!used.empty() && used.front().frame <= completed) {
      free[used.front().key].push_back(used.front().set); used.pop_front();
    }
    xe_keyed_reuse_collect(&pool, completed);
    for (unsigned draw = 0; draw < 64; ++draw) {
      random ^= random << 13; random ^= random >> 17; random ^= random << 5;
      uint32_t kind = random & 255;
      uint32_t key = kind == 0 ? 0 : kind == 255 ? UINT32_MAX : (kind * 65537u ^ ((kind & 1) << 31));
      auto it = free.find(key);
      bool empty = it == free.end() || it->second.empty();
      bool failure = empty && random % 251 == 0;
      VkDescriptorSet expected = VK_NULL_HANDLE;
      if (!failure) {
        if (empty) expected = (VkDescriptorSet)++old_id;
        else { expected = it->second.back(); it->second.pop_back(); }
        used.push_back({frame, key, expected});
      }
      auto* node = xe_keyed_reuse_acquire(&pool, key, frame);
      if (!node) return 2;
      bool threw = false;
      try {
        xe_keyed_reuse_scope scope(&pool);
        if (!node->handle) {
          if (failure) throw 1;
          node->handle = ++new_id;
        }
        if ((VkDescriptorSet)node->handle != expected || !xe_keyed_reuse_commit(&pool)) return 3;
      } catch (int) { threw = true; ++exceptions; }
      if (threw != failure || pool.leased || old_id != new_id) return 4;
    }
    if (!(frame % 997)) {
      used.clear(); free.clear(); if (!xe_keyed_reuse_reset(&pool)) return 5;
    }
  }
  if (!exceptions || !xe_keyed_reuse_destroy(&pool)) return 6;
  std::puts("Keyed reuse reference: 640,000 Vulkan-handle events match legacy per-layout FIFO/LIFO, native failures, exceptions and ten resets");
  return 0;
}
