/* Test-only integer oracle aliases. Production uses unchanged typed images.
 * Untimed image validation, not a compression/performance measurement. C89. */
#ifndef XE_TEST_TRANSFER_IDENTITY_MSAA_VULKAN_VIEWS_H
#define XE_TEST_TRANSFER_IDENTITY_MSAA_VULKAN_VIEWS_H
#include "transfer_uint_vulkan_image.h"
typedef struct identity_ms_image {image raw;VkImageView numeric;} identity_ms_image;
static INLINE VkFormat identity_ms_integer_format(unsigned view)
{
 static const VkFormat formats[4]={VK_FORMAT_R8G8B8A8_UINT,VK_FORMAT_R16G16B16A16_UINT,
  VK_FORMAT_A2B10G10R10_UINT_PACK32,VK_FORMAT_R16G16B16A16_UINT};
 return formats[view];
}
static INLINE VkFormat identity_ms_numeric_format(unsigned view)
{
 static const VkFormat formats[4]={VK_FORMAT_R8G8B8A8_UNORM,VK_FORMAT_R16G16B16A16_UNORM,
  VK_FORMAT_A2B10G10R10_UNORM_PACK32,VK_FORMAT_R16G16B16A16_SFLOAT};
 return formats[view];
}
static INLINE int identity_ms_make_image_usage(identity_ms_image *out,unsigned view,unsigned w,unsigned h,unsigned samples,VkImageUsageFlags extra_usage)
{
 VkImageCreateInfo info={0};VkImageViewCreateInfo create={0};VkMemoryRequirements req;
 VkMemoryAllocateInfo allocation={0};unsigned t;
 info.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;info.flags=VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
 info.imageType=VK_IMAGE_TYPE_2D;info.format=identity_ms_numeric_format(view);
 info.extent.width=w;info.extent.height=h;info.extent.depth=1;
 info.mipLevels=info.arrayLayers=1;info.samples=(VkSampleCountFlagBits)samples;
 info.tiling=VK_IMAGE_TILING_OPTIMAL;info.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|extra_usage;
 if(im.create(g.device,&info,NULL,&out->raw.image)!=VK_SUCCESS)return 0;
 im.requirements(g.device,out->raw.image,&req);
 for(t=0;t<g.memory.memoryTypeCount;++t)if(req.memoryTypeBits&(1u<<t))break;
 if(t==g.memory.memoryTypeCount)return 0;
 allocation.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;allocation.allocationSize=req.size;allocation.memoryTypeIndex=t;
 if(g.vkAllocateMemory(g.device,&allocation,NULL,&out->raw.memory)!=VK_SUCCESS ||
  im.bind(g.device,out->raw.image,out->raw.memory,0)!=VK_SUCCESS)return 0;
 create.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;create.image=out->raw.image;
 create.viewType=VK_IMAGE_VIEW_TYPE_2D;create.format=identity_ms_integer_format(view);
 create.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;create.subresourceRange.levelCount=create.subresourceRange.layerCount=1;
 if(im.view(g.device,&create,NULL,&out->raw.view)!=VK_SUCCESS)return 0;
 create.format=identity_ms_numeric_format(view);
 return im.view(g.device,&create,NULL,&out->numeric)==VK_SUCCESS;
}
static INLINE int identity_ms_make_image(identity_ms_image *out,unsigned view,unsigned w,unsigned h,unsigned samples)
{
 return identity_ms_make_image_usage(out,view,w,h,samples,0);
}
/* Copy exact physical samples between identically formatted images. This is
 * an untimed oracle boundary, never a core/rendering-path intermediate. */
static INLINE void identity_ms_copy(PFN_vkCmdCopyImage copy,VkCommandBuffer command,
 VkImage source,VkImage dest,unsigned width,unsigned height)
{
 VkImageCopy region={0};
 region.srcSubresource.aspectMask=region.dstSubresource.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
 region.srcSubresource.layerCount=region.dstSubresource.layerCount=1;
 region.extent.width=width;region.extent.height=height;region.extent.depth=1;
 copy(command,source,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,dest,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&region);
}
static INLINE void identity_ms_close_image(identity_ms_image *out)
{
 if(out->numeric)im.close_view(g.device,out->numeric,NULL);
 out->numeric=VK_NULL_HANDLE;close_image(&out->raw);
}
#endif
