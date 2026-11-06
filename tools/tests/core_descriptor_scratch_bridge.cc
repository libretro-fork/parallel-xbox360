/* Legacy value-initialized Vulkan vector vs final C89 span assembly. */
#include "core_descriptor_scratch.h"
#include "core_vk_descriptor_image.h"
#include <vector>
#include <cstdio>
extern "C" int xe_test_descriptor_reference() {
  xe_descriptor_scratch storage{};
  std::vector<VkDescriptorImageInfo> old;
  uint32_t random=12345;
  bool ok=true;
  for(unsigned draw=0;draw<10000 && ok;++draw) {
    size_t counts[4];
    for(unsigned group=0;group<4;++group) {
      random^=random<<13; random^=random>>17; random^=random<<5;
      counts[group]=random%65;
    }
    xe_descriptor_plan plan;
    if(!xe_descriptor_prepare(&storage,counts,sizeof(VkDescriptorImageInfo),&plan)) { ok=false; break; }
    auto* records=reinterpret_cast<VkDescriptorImageInfo*>(storage.data);
    old.clear();
    for(unsigned group=0;group<4;++group) {
      if(plan.offsets[group]!=old.size()) ok=false;
      for(size_t i=0;i<counts[group];++i) {
        auto& reference=old.emplace_back();
        uint64_t bits=(UINT64_C(1)<<48)+i*101+group*65536+draw;
        if(group&1) {
          reference.sampler=(VkSampler)bits;
          xe_vk_descriptor_sampler(records+plan.offsets[group]+i,(VkSampler)bits);
        } else {
          reference.imageView=(VkImageView)bits;
          reference.imageLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
          xe_vk_descriptor_texture(records+plan.offsets[group]+i,(VkImageView)bits);
        }
      }
    }
    if(old.size()!=plan.count) ok=false;
    for(size_t i=0;i<old.size();++i)
      if(old[i].sampler!=records[i].sampler || old[i].imageView!=records[i].imageView ||
          old[i].imageLayout!=records[i].imageLayout) ok=false;
  }
  xe_descriptor_scratch_free(&storage);
  if(ok) std::puts("Descriptor reference: 10,000 four-span packets match legacy Vulkan vector fields");
  return ok;
}
