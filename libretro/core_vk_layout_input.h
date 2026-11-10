/* Borrowed Vulkan layout inputs. MSVC C89; BSD license. */
#ifndef XE_CORE_VK_LAYOUT_INPUT_H
#define XE_CORE_VK_LAYOUT_INPUT_H
#include "core_descriptor_scratch.h"
#include <vulkan/vulkan.h>
static INLINE int xe_vk_pipeline_layout_key(size_t pixel_images,size_t pixel_samplers,
      size_t vertex_images,size_t vertex_samplers,uint64_t *key)
{
   if(!key || pixel_images>65535 || pixel_samplers>32767 ||
         vertex_images>65535 || vertex_samplers>32767)return 0;
   *key=(uint64_t)pixel_images|((uint64_t)pixel_samplers<<16)|
      ((uint64_t)vertex_images<<32)|((uint64_t)vertex_samplers<<48);
   return 1;
}
static INLINE int xe_vk_texture_layout_key(size_t images,size_t samplers,int vertex,uint64_t *key)
{
   if(!key || images>65535 || samplers>32767)return 0;
   *key=(uint64_t)images|((uint64_t)samplers<<16)|((uint64_t)(vertex!=0)<<31);
   return 1;
}
/* Exclusive caller scratch; retain high-water capacity across misses. Existing
 * records have no remaining borrower on entry. No copy on growth. */
static INLINE VkDescriptorSetLayoutBinding *xe_vk_layout_bindings(
      xe_descriptor_scratch *scratch,size_t images,size_t samplers,
      VkShaderStageFlags stages)
{
   size_t counts[4]={0,0,0,0},i;
   xe_descriptor_plan plan;
   VkDescriptorSetLayoutBinding *bindings;
   uint64_t ignored;
   if(!xe_vk_texture_layout_key(images,samplers,0,&ignored))return NULL;
   counts[0]=images;counts[1]=samplers;
   if(!xe_descriptor_prepare(scratch,counts,sizeof(*bindings),&plan))return NULL;
   bindings=(VkDescriptorSetLayoutBinding*)scratch->data;
   for(i=0;i<plan.count;++i){
      bindings[i].binding=(uint32_t)i;
      bindings[i].descriptorType=i<images?VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:VK_DESCRIPTOR_TYPE_SAMPLER;
      bindings[i].descriptorCount=1;bindings[i].stageFlags=stages;
      bindings[i].pImmutableSamplers=NULL;
   }
   return bindings;
}
#endif
