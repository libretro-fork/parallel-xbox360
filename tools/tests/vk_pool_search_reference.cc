/* Reference only: std::multimap defines exact ascending/equal-key order. */
#include <cstdint>
#include <cstdio>
#include <map>
#include "core_vk_pool_search.h"
int main() {
  xe_vk_pool_search s{};
  std::multimap<uint32_t, xe_vk_pool_search_node*> reference;
  decltype(reference)::iterator positions[4096];
  xe_vk_pool_search_node* nodes[4096];
  uint32_t random = 97331;
  for (uint32_t i = 0; i < 4096; ++i) {
    nodes[i] = xe_vk_pool_search_acquire(&s); if (!nodes[i]) return 1;
    xe_vk_pool_search_commit(&s);
    xe_vk_pool_search_insert(&s, nodes[i], i % 73);
    positions[i] = reference.emplace(i % 73, nodes[i]);
  }
  for (uint32_t step = 0; step < 300000; ++step) {
    random ^= random << 13; random ^= random >> 17; random ^= random << 5;
    uint32_t id = random % 4096, key = (random >> 16) % 127;
    auto* node = nodes[id];
    xe_vk_pool_search_erase(&s, node); reference.erase(positions[id]);
    if (!(step % 13)) {
      xe_vk_pool_search_release(&s, node);
      node = xe_vk_pool_search_acquire(&s); if (!node) return 1;
      xe_vk_pool_search_commit(&s); nodes[id] = node;
    }
    xe_vk_pool_search_insert(&s, node, key);
    positions[id] = reference.emplace(key, node);
    if (s.maximum != reference.rbegin()->second) return 2;
    if (!(step % 257)) {
      node = s.maximum;
      for (auto it = reference.rbegin(); it != reference.rend(); ++it) {
        if (node != it->second || node->key != it->first) return 3;
        node = xe_vk_pool_search_previous(node);
      }
      if (node) return 4;
    }
  }
  xe_vk_pool_search_destroy(&s);
  std::puts("Pool search reference: 300,000 rekeys across 4,096 records match multimap including equal-key insertion order");
  return 0;
}
