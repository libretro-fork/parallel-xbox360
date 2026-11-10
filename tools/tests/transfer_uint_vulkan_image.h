#ifndef XE_TEST_TRANSFER_UINT_VULKAN_IMAGE_H
#define XE_TEST_TRANSFER_UINT_VULKAN_IMAGE_H
/* Actual integer texture fetch -> fragment output. Untimed C89 correctness. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#define XE_VK_TEST_GRAPHICS
#define XE_VK_TEST_VERTEX_INPUT
#define XE_VK_TEST_NO_BUFFERS
#include "vulkan_test_context.h"
#include "libretro/core_transfer_uint.h"

#define CHECK(x) do {if(!(x)){fprintf(stderr,"Integer image line%u\n",(unsigned)__LINE__);goto done;}}while(0)
static struct {
 PFN_vkCmdBeginRendering begin_rendering;PFN_vkCmdEndRendering end_rendering;
 PFN_vkGetPhysicalDeviceFeatures features;PFN_vkGetPhysicalDeviceImageFormatProperties properties;
 PFN_vkCreateImage create;PFN_vkDestroyImage destroy;
 PFN_vkGetImageMemoryRequirements requirements;PFN_vkBindImageMemory bind;
 PFN_vkCreateImageView view;PFN_vkDestroyImageView close_view;
 PFN_vkCmdCopyBufferToImage upload;PFN_vkCmdCopyImageToBuffer readback;
 PFN_vkCmdSetViewport viewport;PFN_vkCmdSetScissor scissor;
} im;
#define IMAGE_FN(field,name) do {PFN_vkVoidFunction p=g.get(g.instance,#name);memcpy(&im.field,&p,sizeof(p));CHECK(im.field);}while(0)
typedef struct image {VkImage image;VkImageView view;VkDeviceMemory memory;} image;
static INLINE VkFormat format(unsigned f) {
 return f==5 || f==7?VK_FORMAT_R16G16B16A16_UINT:f==14?VK_FORMAT_R32_UINT:f==15?VK_FORMAT_R32G32_UINT:VK_FORMAT_R16G16_UINT;
}
static INLINE void close_image(image *v) {
 if(v->view)im.close_view(g.device,v->view,NULL);
 if(v->image)im.destroy(g.device,v->image,NULL);
 if(v->memory)g.vkFreeMemory(g.device,v->memory,NULL);
 memset(v,0,sizeof(*v));
}
static INLINE int make_image_samples(image *v,VkFormat f,unsigned w,unsigned h,VkImageUsageFlags usage,unsigned samples) {
 VkImageCreateInfo info={0};VkImageViewCreateInfo view={0};
 VkMemoryRequirements req;VkMemoryAllocateInfo allocation={0};unsigned t;
 info.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;info.imageType=VK_IMAGE_TYPE_2D;
 info.format=f;info.extent.width=w;info.extent.height=h;info.extent.depth=1;
 info.mipLevels=info.arrayLayers=1;info.samples=(VkSampleCountFlagBits)samples;
 info.tiling=VK_IMAGE_TILING_OPTIMAL;info.usage=usage;
 if(im.create(g.device,&info,NULL,&v->image)!=VK_SUCCESS)return 0;
 im.requirements(g.device,v->image,&req);
 for(t=0;t<g.memory.memoryTypeCount;++t)if(req.memoryTypeBits&(1u<<t))break;
 if(t==g.memory.memoryTypeCount)return 0;
 allocation.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;allocation.allocationSize=req.size;allocation.memoryTypeIndex=t;
 if(g.vkAllocateMemory(g.device,&allocation,NULL,&v->memory)!=VK_SUCCESS ||
    im.bind(g.device,v->image,v->memory,0)!=VK_SUCCESS)return 0;
 view.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;view.image=v->image;
 view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.format=f;
 view.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
 view.subresourceRange.levelCount=view.subresourceRange.layerCount=1;
 return im.view(g.device,&view,NULL,&v->view)==VK_SUCCESS;
}
static INLINE int make_host(struct buffer *v,size_t bytes) {
 VkBufferCreateInfo info={0};VkMemoryRequirements req;VkMemoryAllocateInfo allocation={0};unsigned t,pass;
 info.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;info.size=bytes;
 info.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT|VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
 if(g.vkCreateBuffer(g.device,&info,NULL,&v->buffer)!=VK_SUCCESS)return 0;
 g.vkGetBufferMemoryRequirements(g.device,v->buffer,&req);
 /* The oracle reads every output and guard word. Prefer cached CPU access;
  * retain the visible-only fallback and normal noncoherent maintenance. */
 for(pass=0;pass<2;++pass) {
  for(t=0;t<g.memory.memoryTypeCount;++t)if((req.memoryTypeBits&(1u<<t)) &&
    (g.memory.memoryTypes[t].propertyFlags&VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
    (pass || (g.memory.memoryTypes[t].propertyFlags&VK_MEMORY_PROPERTY_HOST_CACHED_BIT)))break;
  if(t<g.memory.memoryTypeCount)break;
 }
 if(t==g.memory.memoryTypeCount)return 0;
 allocation.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;allocation.allocationSize=req.size;allocation.memoryTypeIndex=t;
 v->coherent=(g.memory.memoryTypes[t].propertyFlags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)!=0;
 return g.vkAllocateMemory(g.device,&allocation,NULL,&v->memory)==VK_SUCCESS &&
  g.vkBindBufferMemory(g.device,v->buffer,v->memory,0)==VK_SUCCESS &&
  g.vkMapMemory(g.device,v->memory,0,VK_WHOLE_SIZE,0,&v->mapped)==VK_SUCCESS;
}
static INLINE int cache_host(struct buffer *v,int invalidate) {
 VkMappedMemoryRange range={0};if(v->coherent)return 1;
 range.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;range.memory=v->memory;range.size=VK_WHOLE_SIZE;
 return (invalidate?g.vkInvalidateMappedMemoryRanges(g.device,1,&range):g.vkFlushMappedMemoryRanges(g.device,1,&range))==VK_SUCCESS;
}
static INLINE void close_host(struct buffer *v) {
 if(v->mapped)g.vkUnmapMemory(g.device,v->memory);
 if(v->buffer)g.vkDestroyBuffer(g.device,v->buffer,NULL);
 if(v->memory)g.vkFreeMemory(g.device,v->memory,NULL);
}
static INLINE void transition(VkCommandBuffer command,VkImage v,VkImageLayout old,VkImageLayout next,
 VkPipelineStageFlags src,VkPipelineStageFlags dst,VkAccessFlags read,VkAccessFlags write) {
 VkImageMemoryBarrier b={0};b.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
 b.oldLayout=old;b.newLayout=next;b.srcAccessMask=read;b.dstAccessMask=write;
 b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.image=v;
 b.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;b.subresourceRange.levelCount=b.subresourceRange.layerCount=1;
 g.vkCmdPipelineBarrier(command,src,dst,0,0,NULL,0,NULL,1,&b);
}

static INLINE int make_image(image *v,VkFormat f,unsigned w,unsigned h,VkImageUsageFlags usage) {
 return make_image_samples(v,f,w,h,usage,1);
}
#endif
