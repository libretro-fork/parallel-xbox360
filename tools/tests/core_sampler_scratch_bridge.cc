/* Differential legacy vector snapshot behavior and actual Vulkan handle ABI. */
#include "core_sampler_scratch.h"
#include "xenia/gpu/vulkan/vulkan_texture_cache.h"
#include <vector>
#include <utility>
#include <cstdio>
static_assert(sizeof(VkSampler)==sizeof(uint64_t));
extern "C" int xe_test_sampler_reference() {
  using Pair=std::pair<xe::gpu::vulkan::VulkanTextureCache::SamplerParameters,VkSampler>;
  std::vector<Pair> current,written;
  xe_sampler_stage stage{};
  uint32_t random=12345;
  bool ok=true;
  for(unsigned draw=0;draw<10000 && ok;++draw) {
    random^=random<<13; random^=random>>17; random^=random<<5;
    size_t count=random%257;
    if(!xe_sampler_begin(&stage,count)) { ok=false; break; }
    current.clear();
    for(size_t i=0;i<count;++i) {
      xe::gpu::vulkan::VulkanTextureCache::SamplerParameters parameters;
      parameters.value=uint32_t(i*37+draw%3);
      uint64_t bits=(UINT64_C(1)<<48)+i*101+draw%3;
      VkSampler handle=(VkSampler)bits;
      current.emplace_back(parameters,handle);
      auto& record=xe_sampler_current(&stage)->data[i];
      record.parameters=parameters.value; record.sampler=(uint64_t)handle;
      if((VkSampler)record.sampler!=handle) ok=false;
    }
    if(bool(xe_sampler_matches(&stage))!=(current==written)) ok=false;
    /* Repeated same-draw binding calls and unsuccessful descriptor allocation
     * don't implicitly change the written snapshot. */
    if((draw%5)!=0) {
      written=current; xe_sampler_commit(&stage); xe_sampler_commit(&stage);
      if(!xe_sampler_matches(&stage) || xe_sampler_current(&stage)->count!=count) ok=false;
    }
  }
  xe_sampler_stage_free(&stage);
  if(ok) std::puts("Sampler reference: 10,000 legacy-vector draws and 64-bit Vulkan handles match");
  return ok;
}
