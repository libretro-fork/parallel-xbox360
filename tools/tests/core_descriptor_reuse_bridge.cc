/* Differential native failure/exception/reset model against old deque+vectors. */
#include "core_descriptor_reuse.h"
#include <vulkan/vulkan_core.h>
#include <deque>
#include <vector>
#include <utility>
#include <cstdio>
struct Used { uint64_t frame; unsigned layout; VkDescriptorSet set; };
static_assert(sizeof(VkDescriptorSet)==8);
extern "C" int xe_test_descriptor_reuse_reference() {
  xe_descriptor_reuse pools[3]{};
  std::deque<Used> used;
  std::deque<std::pair<uint64_t,VkDescriptorSet>> constants;
  std::vector<VkDescriptorSet> free[3];
  uint64_t old_id=UINT64_C(1)<<48,new_id=old_id;
  uint32_t random=12345;
  bool ok=true;
  for(auto& pool:pools) if(!xe_descriptor_reuse_warm(&pool)) ok=false;
  for(uint64_t frame=1;frame<=10000 && ok;++frame) {
    uint64_t completed=frame>3 ? frame-3 : 0;
    while(!used.empty() && used.front().frame<=completed) {
      free[used.front().layout].push_back(used.front().set); used.pop_front();
    }
    while(!constants.empty() && constants.front().first<=completed) {
      free[2].push_back(constants.front().second); constants.pop_front();
    }
    for(auto& pool:pools) xe_descriptor_reuse_collect(&pool,completed);
    for(unsigned draw=0;draw<32 && ok;++draw) {
      random^=random<<13; random^=random>>17; random^=random<<5;
      unsigned kind=random%3;
      VkDescriptorSet expected=VK_NULL_HANDLE;
      bool native=free[kind].empty(),failure=native && (random%97)==0;
      if(!native) { expected=free[kind].back(); free[kind].pop_back(); }
      else if(!failure) expected=(VkDescriptorSet)++old_id;
      if(!failure) {
        if(kind==2) constants.emplace_back(frame,expected);
        else used.push_back({frame,kind,expected});
      }
      auto* node=xe_descriptor_reuse_acquire(pools+kind,frame);
      if(!node) { ok=false; break; }
      bool threw=false;
      try {
        xe_descriptor_reuse_scope scope(pools+kind);
        if(!node->handle) {
          if(failure) throw 1; // Existing C++ driver allocator exception boundary.
          node->handle=++new_id;
        }
        if((VkDescriptorSet)node->handle!=expected || !xe_descriptor_reuse_commit(pools+kind)) ok=false;
      } catch(int) { threw=true; }
      if(threw!=failure || pools[kind].leased) ok=false;
    }
    /* Reset only after this fixture's simulated GPU drain; stale handle values
     * must be forgotten while retained CPU pages are reused. */
    if(!(frame%1000)) {
      used.clear(); constants.clear(); for(auto& list:free) list.clear();
      for(auto& pool:pools) if(!xe_descriptor_reuse_reset(&pool)) ok=false;
    }
    if(old_id!=new_id) ok=false;
  }
  for(auto& pool:pools) xe_descriptor_reuse_destroy(&pool);
  if(ok) std::puts("Descriptor reference: 320,000 Vulkan-handle events match legacy FIFO/LIFO, failures and pool resets");
  return ok;
}
