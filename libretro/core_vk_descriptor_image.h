/* Actual Vulkan C records, filled once in final borrowed scratch. */
#ifndef XE_CORE_VK_DESCRIPTOR_IMAGE_H
#define XE_CORE_VK_DESCRIPTOR_IMAGE_H
#include <retro_inline.h>
#include <vulkan/vulkan_core.h>
static INLINE void xe_vk_descriptor_texture(VkDescriptorImageInfo *info, VkImageView image) {
  info->sampler = VK_NULL_HANDLE;
  info->imageView = image;
  info->imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}
static INLINE void xe_vk_descriptor_sampler(VkDescriptorImageInfo *info, VkSampler sampler) {
  info->sampler = sampler;
  info->imageView = VK_NULL_HANDLE;
  info->imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
}
#endif
