/* C89 GPU differential: original scratch shader vs direct borrowed views. */
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include "libretro/core_gpu_downscale.h"
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
   VkInstance instance;
   VkDevice device;
   VkPhysicalDeviceMemoryProperties memory;
} g;
#define LOAD(n) do { PFN_vkVoidFunction p=g.get(g.instance,#n); memcpy(&g.n,&p,sizeof(p)); if (!g.n) { fprintf(stderr,"Missing %s\n",#n); goto done; } } while (0)
#define OK(x) do { VkResult r=(x); if (r!=VK_SUCCESS) { fprintf(stderr,"Vulkan %d at line %u\n",(int)r,(unsigned)__LINE__); goto done; } } while (0)
struct buffer { VkBuffer buffer; VkDeviceMemory memory; void *mapped; int coherent; };
static int make_buffer(struct buffer *b, size_t bytes)
{
   VkBufferCreateInfo info = {0};
   VkMemoryRequirements requirements;
   VkMemoryAllocateInfo allocation = {0};
   uint32_t type, pass;
   info.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
   info.size=bytes; info.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
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
static int shader(const char *path, VkShaderModule *module)
{
   FILE *file=fopen(path,"rb");
   long size;
   uint32_t *words;
   VkShaderModuleCreateInfo info = {0};
   VkResult result;
   if (!file) return 0;
   if (fseek(file,0,SEEK_END) || (size=ftell(file))<=0 || size>1048576 || (size&3) || fseek(file,0,SEEK_SET)) { fclose(file); return 0; }
   words=(uint32_t *)malloc((size_t)size);
   if (!words) { fclose(file); return 0; }
   if (fread(words,1,(size_t)size,file)!=(size_t)size) { fclose(file); free(words); return 0; }
   fclose(file);
   info.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO; info.codeSize=(size_t)size; info.pCode=words;
   result=g.vkCreateShaderModule(g.device,&info,NULL,module);
   free(words); return result==VK_SUCCESS;
}
int main(int argc, char **argv)
{
#ifdef _WIN32
   HMODULE loader=LoadLibraryA("vulkan-1.dll");
   FARPROC entry;
#else
   void *loader=dlopen("libvulkan.so.1",RTLD_NOW|RTLD_LOCAL), *entry;
#endif
   VkApplicationInfo app = {0}; VkInstanceCreateInfo instance_info = {0};
   VkPhysicalDevice physical[8]; VkPhysicalDeviceProperties properties;
   VkQueueFamilyProperties families[32]; VkDeviceQueueCreateInfo queue_info = {0};
   VkDeviceCreateInfo device_info = {0}; VkQueue queue=VK_NULL_HANDLE;
   VkDescriptorSetLayoutBinding bindings[2]; VkDescriptorSetLayoutCreateInfo set_info = {0};
   VkDescriptorSetLayout set_layout=VK_NULL_HANDLE, set_layouts[3];
   VkPushConstantRange push; VkPipelineLayoutCreateInfo layout_info = {0};
   VkPipelineLayout layout=VK_NULL_HANDLE;
   VkShaderModule modules[2]={0}; VkPipeline pipelines[2]={0};
   VkComputePipelineCreateInfo pipeline_info = {0};
   VkDescriptorPoolSize pool_size; VkDescriptorPoolCreateInfo pool_info = {0};
   VkDescriptorPool descriptor_pool=VK_NULL_HANDLE; VkDescriptorSet sets[3];
   VkDescriptorSetAllocateInfo set_allocate = {0};
   VkCommandPoolCreateInfo command_pool_info = {0}; VkCommandPool command_pool=VK_NULL_HANDLE;
   VkCommandBufferAllocateInfo command_allocate = {0}; VkCommandBuffer command;
   VkCommandBufferBeginInfo begin = {0}; VkFenceCreateInfo fence_info = {0};
   VkFence fence=VK_NULL_HANDLE; VkSubmitInfo submit = {0};
   struct buffer buffers[3];
   float priority=1.0f;
   uint32_t count, family, i, pixel, sx, sy, half, offset_case, constants[7];
   uint32_t cases=0, chunks, remaining, area, tile_bytes, bytes, alignment;
   uint32_t seed=UINT32_C(1234567);
   uint64_t source_offset, destination;
   xe_gpu_downscale_view view = {0};
   size_t byte;
   int result=1;
   const uint32_t offsets[4]={0,64,128,256};
   const size_t source_size=2u*1024u*1024u, output_size=65536;
   memset(buffers,0,sizeof(buffers));
   if (argc!=3 || !loader) goto done;
#ifdef _WIN32
   entry=GetProcAddress(loader,"vkGetInstanceProcAddr");
#else
   entry=dlsym(loader,"vkGetInstanceProcAddr");
#endif
   memcpy(&g.get,&entry,sizeof(g.get)); if (!g.get) goto done;
   LOAD(vkCreateInstance);
   app.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO; app.apiVersion=VK_API_VERSION_1_2;
   instance_info.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO; instance_info.pApplicationInfo=&app;
   OK(g.vkCreateInstance(&instance_info,NULL,&g.instance));
   LOAD(vkDestroyInstance); LOAD(vkEnumeratePhysicalDevices);
   LOAD(vkGetPhysicalDeviceQueueFamilyProperties); LOAD(vkGetPhysicalDeviceMemoryProperties);
   LOAD(vkGetPhysicalDeviceProperties); LOAD(vkCreateDevice);
   count=8; OK(g.vkEnumeratePhysicalDevices(g.instance,&count,physical)); if (!count) goto done;
   count=32; g.vkGetPhysicalDeviceQueueFamilyProperties(physical[0],&count,families);
   for (family=0;family<count;++family) if (families[family].queueFlags&VK_QUEUE_COMPUTE_BIT) break;
   if (family==count) goto done;
   g.vkGetPhysicalDeviceMemoryProperties(physical[0],&g.memory);
   g.vkGetPhysicalDeviceProperties(physical[0],&properties);
   queue_info.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO; queue_info.queueFamilyIndex=family;
   queue_info.queueCount=1; queue_info.pQueuePriorities=&priority;
   device_info.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO; device_info.queueCreateInfoCount=1;
   device_info.pQueueCreateInfos=&queue_info;
   OK(g.vkCreateDevice(physical[0],&device_info,NULL,&g.device));
   LOAD(vkDestroyDevice); LOAD(vkGetDeviceQueue); LOAD(vkCreateBuffer); LOAD(vkDestroyBuffer);
   LOAD(vkGetBufferMemoryRequirements); LOAD(vkAllocateMemory); LOAD(vkFreeMemory);
   LOAD(vkBindBufferMemory); LOAD(vkMapMemory); LOAD(vkUnmapMemory);
   LOAD(vkFlushMappedMemoryRanges); LOAD(vkInvalidateMappedMemoryRanges);
   LOAD(vkCreateShaderModule); LOAD(vkDestroyShaderModule);
   LOAD(vkCreateDescriptorSetLayout); LOAD(vkDestroyDescriptorSetLayout);
   LOAD(vkCreatePipelineLayout); LOAD(vkDestroyPipelineLayout);
   LOAD(vkCreateComputePipelines); LOAD(vkDestroyPipeline);
   LOAD(vkCreateDescriptorPool); LOAD(vkDestroyDescriptorPool);
   LOAD(vkAllocateDescriptorSets); LOAD(vkUpdateDescriptorSets);
   LOAD(vkCreateCommandPool); LOAD(vkDestroyCommandPool); LOAD(vkAllocateCommandBuffers);
   LOAD(vkResetCommandPool); LOAD(vkBeginCommandBuffer); LOAD(vkEndCommandBuffer);
   LOAD(vkCmdPipelineBarrier); LOAD(vkCmdBindPipeline); LOAD(vkCmdBindDescriptorSets);
   LOAD(vkCmdPushConstants); LOAD(vkCmdDispatch); LOAD(vkCreateFence); LOAD(vkDestroyFence);
   LOAD(vkResetFences); LOAD(vkQueueSubmit); LOAD(vkWaitForFences);
   g.vkGetDeviceQueue(g.device,family,0,&queue);
   if (!make_buffer(&buffers[0],source_size) || !make_buffer(&buffers[1],output_size) || !make_buffer(&buffers[2],output_size)) goto done;
   for (i=0;i<source_size/4;++i) {
      seed=seed*UINT32_C(1664525)+UINT32_C(1013904223);
      ((uint32_t *)buffers[0].mapped)[i]=seed;
   }
   if (!cache_buffer(&buffers[0],0)) goto done;
   memset(bindings,0,sizeof(bindings));
   for (i=0;i<2;++i) { bindings[i].binding=i; bindings[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      bindings[i].descriptorCount=1; bindings[i].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT; }
   set_info.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO; set_info.bindingCount=2; set_info.pBindings=bindings;
   OK(g.vkCreateDescriptorSetLayout(g.device,&set_info,NULL,&set_layout));
   push.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT; push.offset=0; push.size=sizeof(constants);
   layout_info.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO; layout_info.setLayoutCount=1;
   layout_info.pSetLayouts=&set_layout; layout_info.pushConstantRangeCount=1; layout_info.pPushConstantRanges=&push;
   OK(g.vkCreatePipelineLayout(g.device,&layout_info,NULL,&layout));
   for (i=0;i<2;++i) {
      if (!shader(argv[i+1],&modules[i])) goto done;
      pipeline_info.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
      pipeline_info.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
      pipeline_info.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT; pipeline_info.stage.module=modules[i];
      pipeline_info.stage.pName="main"; pipeline_info.layout=layout;
      OK(g.vkCreateComputePipelines(g.device,VK_NULL_HANDLE,1,&pipeline_info,NULL,&pipelines[i]));
   }
   pool_size.type=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; pool_size.descriptorCount=6;
   pool_info.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO; pool_info.maxSets=3;
   pool_info.poolSizeCount=1; pool_info.pPoolSizes=&pool_size;
   OK(g.vkCreateDescriptorPool(g.device,&pool_info,NULL,&descriptor_pool));
   for (i=0;i<3;++i) set_layouts[i]=set_layout;
   set_allocate.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO; set_allocate.descriptorPool=descriptor_pool;
   set_allocate.descriptorSetCount=3; set_allocate.pSetLayouts=set_layouts;
   OK(g.vkAllocateDescriptorSets(g.device,&set_allocate,sets));
   command_pool_info.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO; command_pool_info.queueFamilyIndex=family;
   OK(g.vkCreateCommandPool(g.device,&command_pool_info,NULL,&command_pool));
   command_allocate.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO; command_allocate.commandPool=command_pool;
   command_allocate.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY; command_allocate.commandBufferCount=1;
   OK(g.vkAllocateCommandBuffers(g.device,&command_allocate,&command));
   fence_info.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO; OK(g.vkCreateFence(g.device,&fence_info,NULL,&fence));
   begin.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
   submit.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO; submit.commandBufferCount=1; submit.pCommandBuffers=&command;
   alignment=(uint32_t)properties.limits.minStorageBufferOffsetAlignment;
   if (alignment<256) alignment=256;
   for (pixel=0;pixel<4;++pixel) for (sx=1;sx<=8;++sx) for (sy=1;sy<=8;++sy)
   for (half=0;half<2;++half) for (offset_case=0;offset_case<4;++offset_case) {
      VkMemoryBarrier barrier = {0};
      VkDescriptorBufferInfo infos[2]; VkWriteDescriptorSet writes[2];
      tile_bytes=1024u<<pixel; bytes=tile_bytes*2; area=sx*sy;
      destination=offsets[offset_case]; source_offset=destination*area;
      memset(buffers[1].mapped,0xA5,output_size); memset(buffers[2].mapped,0xA5,output_size);
      if (!cache_buffer(&buffers[1],0) || !cache_buffer(&buffers[2],0)) goto done;
      OK(g.vkResetCommandPool(g.device,command_pool,0)); OK(g.vkBeginCommandBuffer(command,&begin));
      barrier.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER; barrier.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT;
      barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
      g.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&barrier,0,NULL,0,NULL);
      memset(constants,0,sizeof(constants)); constants[0]=sx; constants[1]=sy; constants[2]=pixel;
      constants[3]=2; constants[4]=(uint32_t)source_offset; constants[5]=half;
      chunks=0; remaining=2;
      do {
         uint32_t set_index=chunks;
         infos[0].buffer=buffers[0].buffer; infos[0].offset=0; infos[0].range=source_size;
         infos[1].buffer=buffers[chunks ? 2 : 1].buffer; infos[1].offset=0; infos[1].range=output_size;
         if (chunks) {
            if (!xe_gpu_downscale_plan(source_offset,destination,source_size,output_size,
                tile_bytes,area,remaining,alignment,2u*1024u*1024u,1,&view)) goto done;
            infos[0].offset=view.source_offset; infos[0].range=view.source_size;
            infos[1].offset=view.destination_offset; infos[1].range=view.destination_size;
            constants[3]=view.tiles; constants[4]=view.source_bias; constants[6]=view.destination_bias;
         }
         memset(writes,0,sizeof(writes));
         for (i=0;i<2;++i) { writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet=sets[set_index]; writes[i].dstBinding=i; writes[i].descriptorCount=1;
            writes[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[i].pBufferInfo=&infos[i]; }
         g.vkUpdateDescriptorSets(g.device,2,writes,0,NULL);
         g.vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipelines[chunks ? 1 : 0]);
         g.vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&sets[set_index],0,NULL);
         g.vkCmdPushConstants(command,layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(constants),constants);
         g.vkCmdDispatch(command,constants[3],1,1);
         if (chunks) { remaining-=view.tiles; source_offset+=(uint64_t)view.tiles*tile_bytes*area;
            destination+=(uint64_t)view.tiles*tile_bytes; }
         ++chunks;
      } while (remaining);
      barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT; barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
      g.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,NULL,0,NULL);
      OK(g.vkEndCommandBuffer(command)); OK(g.vkResetFences(g.device,1,&fence));
      OK(g.vkQueueSubmit(queue,1,&submit,fence));
      if (g.vkWaitForFences(g.device,1,&fence,VK_TRUE,(uint64_t)1000000000*10)!=VK_SUCCESS) {
         /* Let process teardown release pending GPU work; do not destroy its
          * resources while a failed or timed-out submission may use them. */
         fprintf(stderr,"GPU completion failed\n"); exit(1);
      }
      if (!cache_buffer(&buffers[1],1) || !cache_buffer(&buffers[2],1)) goto done;
      if (memcmp(buffers[1].mapped,(unsigned char *)buffers[2].mapped+offsets[offset_case],bytes)) {
         fprintf(stderr,"Mismatch: pixel=%u scale=%ux%u half=%u offset=%u\n",pixel,sx,sy,half,offsets[offset_case]); goto done; }
      for (byte=0;byte<output_size;++byte) if ((byte<offsets[offset_case] || byte>=offsets[offset_case]+bytes) && ((unsigned char *)buffers[2].mapped)[byte]!=0xA5) {
         fprintf(stderr,"Guard overwritten: %u\n",(unsigned)byte); goto done; }
      ++cases;
   }
   printf("PASS: %u Vulkan GPU differential cases, 8/16/32/64bpp, 1-8x axes, half offsets, biased borrowed views, split dispatches and untouched guards (%s)\n",cases,properties.deviceName);
   result=0;
done:
   if (g.device) {
      if (fence) g.vkDestroyFence(g.device,fence,NULL);
      if (command_pool) g.vkDestroyCommandPool(g.device,command_pool,NULL);
      if (descriptor_pool) g.vkDestroyDescriptorPool(g.device,descriptor_pool,NULL);
      for (i=0;i<2;++i) { if (pipelines[i]) g.vkDestroyPipeline(g.device,pipelines[i],NULL);
         if (modules[i]) g.vkDestroyShaderModule(g.device,modules[i],NULL); }
      if (layout) g.vkDestroyPipelineLayout(g.device,layout,NULL);
      if (set_layout) g.vkDestroyDescriptorSetLayout(g.device,set_layout,NULL);
      for (i=0;i<3;++i) close_buffer(&buffers[i]);
      g.vkDestroyDevice(g.device,NULL);
   }
   if (g.instance && g.vkDestroyInstance) g.vkDestroyInstance(g.instance,NULL);
#ifdef _WIN32
   if (loader) FreeLibrary(loader);
#else
   if (loader) dlclose(loader);
#endif
   return result;
}
