#ifndef XE_TEST_VULKAN_CONTEXT_H
#define XE_TEST_VULKAN_CONTEXT_H
/* Shared C89 Vulkan test context; no production dependency. */
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "shader_compile_bench.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#define FN(n) PFN_##n n
static struct {
   PFN_vkGetInstanceProcAddr get;
   FN(vkCreateInstance); FN(vkDestroyInstance); FN(vkEnumeratePhysicalDevices);
   FN(vkGetPhysicalDeviceQueueFamilyProperties); FN(vkGetPhysicalDeviceMemoryProperties);
   FN(vkGetPhysicalDeviceProperties); FN(vkCreateDevice); FN(vkDestroyDevice);
   FN(vkGetDeviceQueue); FN(vkCreateBuffer); FN(vkDestroyBuffer);
   FN(vkGetBufferMemoryRequirements); FN(vkAllocateMemory); FN(vkFreeMemory);
   FN(vkBindBufferMemory); FN(vkMapMemory); FN(vkUnmapMemory);
   FN(vkFlushMappedMemoryRanges); FN(vkInvalidateMappedMemoryRanges);
   FN(vkCreateShaderModule); FN(vkDestroyShaderModule);
   FN(vkCreateDescriptorSetLayout); FN(vkDestroyDescriptorSetLayout);
   FN(vkCreatePipelineLayout); FN(vkDestroyPipelineLayout);
   FN(vkCreateComputePipelines); FN(vkDestroyPipeline);
   FN(vkCreateDescriptorPool); FN(vkDestroyDescriptorPool);
   FN(vkAllocateDescriptorSets); FN(vkUpdateDescriptorSets);
   FN(vkCreateCommandPool); FN(vkDestroyCommandPool); FN(vkAllocateCommandBuffers);
   FN(vkResetCommandPool); FN(vkBeginCommandBuffer); FN(vkEndCommandBuffer);
   FN(vkCmdPipelineBarrier); FN(vkCmdBindPipeline); FN(vkCmdBindDescriptorSets);
   FN(vkCmdPushConstants); FN(vkCmdDispatch); FN(vkCreateFence); FN(vkDestroyFence);
   FN(vkResetFences); FN(vkQueueSubmit); FN(vkWaitForFences);
#ifdef XE_VK_TEST_GRAPHICS
   FN(vkGetPhysicalDeviceFeatures2); FN(vkCreateRenderPass); FN(vkDestroyRenderPass);
   FN(vkCreateFramebuffer); FN(vkDestroyFramebuffer); FN(vkCreateGraphicsPipelines);
   FN(vkCmdBeginRenderPass); FN(vkCmdEndRenderPass); FN(vkCmdDraw);
   FN(vkCmdBindTransformFeedbackBuffersEXT); FN(vkCmdBeginTransformFeedbackEXT); FN(vkCmdEndTransformFeedbackEXT);
#endif
   VkInstance instance;
   VkDevice device;
   VkPhysicalDeviceMemoryProperties memory;
} g;
#define LOAD(n) do { PFN_vkVoidFunction p=g.get(g.instance,#n); memcpy(&g.n,&p,sizeof(p)); if (!g.n) { fprintf(stderr,"Missing %s\n",#n); goto done; } } while (0)
#define OK(x) do { VkResult r=(x); if (r!=VK_SUCCESS) { fprintf(stderr,"Vulkan %d at line %u\n",(int)r,(unsigned)__LINE__); goto done; } } while (0)
struct buffer { VkBuffer buffer; VkDeviceMemory memory; void *mapped; int coherent; };
#ifndef XE_VK_TEST_NO_BUFFERS
static int make_buffer(struct buffer *b, size_t bytes)
{
   VkBufferCreateInfo info = {0};
   VkMemoryRequirements requirements;
   VkMemoryAllocateInfo allocation = {0};
   uint32_t type, pass;
   info.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
   info.size=bytes; info.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
#ifdef XE_VK_TEST_GRAPHICS
   info.usage|=VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT;
#endif
   if (g.vkCreateBuffer(g.device,&info,NULL,&b->buffer)!=VK_SUCCESS) return 0;
   g.vkGetBufferMemoryRequirements(g.device,b->buffer,&requirements);
   for (pass=0;pass<2;++pass) for (type=0;type<g.memory.memoryTypeCount;++type) {
      VkMemoryPropertyFlags flags=g.memory.memoryTypes[type].propertyFlags;
      if (!(requirements.memoryTypeBits & (1u<<type)) || !(flags&VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) continue;
      if (!pass && !(flags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) continue;
      allocation.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
      allocation.allocationSize=requirements.size; allocation.memoryTypeIndex=type;
      if (g.vkAllocateMemory(g.device,&allocation,NULL,&b->memory)!=VK_SUCCESS) return 0;
      b->coherent=(flags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)!=0;
      if (g.vkBindBufferMemory(g.device,b->buffer,b->memory,0)!=VK_SUCCESS) return 0;
      return g.vkMapMemory(g.device,b->memory,0,VK_WHOLE_SIZE,0,&b->mapped)==VK_SUCCESS;
   }
   return 0;
}
static void close_buffer(struct buffer *b)
{
   if (b->mapped) g.vkUnmapMemory(g.device,b->memory);
   if (b->buffer) g.vkDestroyBuffer(g.device,b->buffer,NULL);
   if (b->memory) g.vkFreeMemory(g.device,b->memory,NULL);
}
static int cache_buffer(struct buffer *b, int invalidate)
{
   VkMappedMemoryRange range = {0};
   if (b->coherent) return 1;
   range.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
   range.memory=b->memory; range.size=VK_WHOLE_SIZE;
   return (invalidate ? g.vkInvalidateMappedMemoryRanges(g.device,1,&range)
                      : g.vkFlushMappedMemoryRanges(g.device,1,&range))==VK_SUCCESS;
}
#endif
#ifndef XE_VK_TEST_SHADER_MAX_BYTES
#define XE_VK_TEST_SHADER_MAX_BYTES 1048576
#endif
static int shader(const char *path, VkShaderModule *module)
{
   FILE *file=fopen(path,"rb");
   long size;
   uint32_t *words;
   VkShaderModuleCreateInfo info = {0};
   VkResult result;
   if (!file) return 0;
   if (fseek(file,0,SEEK_END) || (size=ftell(file))<=0 || size>XE_VK_TEST_SHADER_MAX_BYTES || (size&3) || fseek(file,0,SEEK_SET)) { fclose(file); return 0; }
   words=(uint32_t *)malloc((size_t)size);
   if (!words) { fclose(file); return 0; }
   if (fread(words,1,(size_t)size,file)!=(size_t)size) { fclose(file); free(words); return 0; }
   fclose(file);
   info.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO; info.codeSize=(size_t)size; info.pCode=words;
   XE_COMPILE_BEGIN();
   result=g.vkCreateShaderModule(g.device,&info,NULL,module);
   XE_COMPILE_END("vulkan.module",xe_compile_case);
   free(words); return result==VK_SUCCESS;
}
#endif
