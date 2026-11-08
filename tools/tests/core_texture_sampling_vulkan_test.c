/* Actual production sampling on Vulkan; C89, no window or game required. */
#include "vulkan_test_context.h"
static struct {
   FN(vkCreateImage); FN(vkDestroyImage); FN(vkGetImageMemoryRequirements);
   FN(vkBindImageMemory); FN(vkCreateImageView); FN(vkDestroyImageView);
   FN(vkCreateSampler); FN(vkDestroySampler); FN(vkCmdClearColorImage);
} images_api;
#define IMAGE_FN(n) do {PFN_vkVoidFunction p=g.get(g.instance,#n);memcpy(&images_api.n,&p,sizeof(p));if(!images_api.n)goto done;}while(0)
struct image { VkImage image; VkDeviceMemory memory; VkImageView view; };
static int make_image(struct image *out,unsigned volume)
{
   VkImageCreateInfo info={0};VkMemoryRequirements req;
   VkMemoryAllocateInfo allocation={0};VkImageViewCreateInfo view={0};uint32_t type;
   info.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
   info.imageType=volume?VK_IMAGE_TYPE_3D:VK_IMAGE_TYPE_2D;
   info.format=VK_FORMAT_R32G32B32A32_SFLOAT;
   info.extent.width=info.extent.height=info.extent.depth=1;
   info.mipLevels=info.arrayLayers=1;info.samples=VK_SAMPLE_COUNT_1_BIT;
   info.tiling=VK_IMAGE_TILING_OPTIMAL;info.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
   if(images_api.vkCreateImage(g.device,&info,NULL,&out->image)!=VK_SUCCESS)return 0;
   images_api.vkGetImageMemoryRequirements(g.device,out->image,&req);
   for(type=0;type<g.memory.memoryTypeCount;++type)if(req.memoryTypeBits&(1u<<type))break;
   if(type==g.memory.memoryTypeCount)return 0;
   allocation.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;allocation.allocationSize=req.size;allocation.memoryTypeIndex=type;
   if(g.vkAllocateMemory(g.device,&allocation,NULL,&out->memory)!=VK_SUCCESS || images_api.vkBindImageMemory(g.device,out->image,out->memory,0)!=VK_SUCCESS)return 0;
   view.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;view.image=out->image;
   view.viewType=volume?VK_IMAGE_VIEW_TYPE_3D:VK_IMAGE_VIEW_TYPE_2D;view.format=info.format;
   view.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;view.subresourceRange.levelCount=view.subresourceRange.layerCount=1;
   return images_api.vkCreateImageView(g.device,&view,NULL,&out->view)==VK_SUCCESS;
}
int main(int argc,char **argv)
{
#ifdef _WIN32
   HMODULE loader=LoadLibraryA("vulkan-1.dll");FARPROC entry;
#else
   void *loader=dlopen("libvulkan.so.1",RTLD_NOW|RTLD_LOCAL),*entry;
#endif
   VkApplicationInfo app={0};VkInstanceCreateInfo instance_info={0};
   VkPhysicalDevice physical[8];VkPhysicalDeviceProperties properties;
   VkQueueFamilyProperties families[32];VkDeviceQueueCreateInfo queue_info={0};
   VkDeviceCreateInfo device_info={0};VkQueue queue=VK_NULL_HANDLE;
   VkDescriptorSetLayoutBinding bindings[4];VkDescriptorSetLayoutCreateInfo set_info={0};
   VkDescriptorSetLayout set_layout=VK_NULL_HANDLE;VkPipelineLayoutCreateInfo layout_info={0};
   VkPushConstantRange push;VkPipelineLayout layout=VK_NULL_HANDLE;
   VkShaderModule modules[8]={0};VkPipeline pipelines[8]={0};VkComputePipelineCreateInfo pipeline_info={0};
   VkDescriptorPoolSize sizes[3];VkDescriptorPoolCreateInfo pool_info={0};
   VkDescriptorPool descriptor_pool=VK_NULL_HANDLE;VkDescriptorSet set;
   VkDescriptorSetAllocateInfo set_allocate={0};
   VkCommandPoolCreateInfo command_pool_info={0};VkCommandPool command_pool=VK_NULL_HANDLE;
   VkCommandBufferAllocateInfo command_allocate={0};VkCommandBuffer command;
   VkCommandBufferBeginInfo begin={0};VkFenceCreateInfo fence_info={0};VkFence fence=VK_NULL_HANDLE;
   VkSubmitInfo submit={0};VkSamplerCreateInfo sampler_info={0};VkSampler sampler=VK_NULL_HANDLE;
   struct buffer buffers[1];struct image images[4];VkImageMemoryBarrier image_barriers[4];
   VkImageSubresourceRange range={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
   const float colors[2][4]={{.25f,.5f,.75f,1.0f},{-.5f,-.25f,.125f,.625f}};
   float priority=1.0f,expected[8],sample,first;
   uint32_t count,family,i,j,which,flags,repeat,constants[2],cases=0;
   int result=1;char path[512];size_t byte;
   memset(buffers,0,sizeof(buffers));memset(images,0,sizeof(images));
   if(argc!=2 || strlen(argv[1])>400 || !loader)goto done;
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

   IMAGE_FN(vkCreateImage);IMAGE_FN(vkDestroyImage);IMAGE_FN(vkGetImageMemoryRequirements);
   IMAGE_FN(vkBindImageMemory);IMAGE_FN(vkCreateImageView);IMAGE_FN(vkDestroyImageView);
   IMAGE_FN(vkCreateSampler);IMAGE_FN(vkDestroySampler);IMAGE_FN(vkCmdClearColorImage);
   if(!make_buffer(&buffers[0],4096))goto done;
   for(i=0;i<4;++i)if(!make_image(&images[i],i>>1))goto done;
   sampler_info.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
   sampler_info.magFilter=sampler_info.minFilter=VK_FILTER_NEAREST;
   sampler_info.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;
   sampler_info.addressModeU=sampler_info.addressModeV=sampler_info.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
   OK(images_api.vkCreateSampler(g.device,&sampler_info,NULL,&sampler));
   memset(bindings,0,sizeof(bindings));
   for(i=0;i<4;++i){bindings[i].binding=i;bindings[i].descriptorCount=1;bindings[i].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;
      bindings[i].descriptorType=i<2?VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:i==2?VK_DESCRIPTOR_TYPE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;}
   set_info.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;set_info.bindingCount=4;set_info.pBindings=bindings;
   OK(g.vkCreateDescriptorSetLayout(g.device,&set_info,NULL,&set_layout));
   push.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;push.offset=0;push.size=8;
   layout_info.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;layout_info.setLayoutCount=1;
   layout_info.pSetLayouts=&set_layout;layout_info.pushConstantRangeCount=1;layout_info.pPushConstantRanges=&push;
   OK(g.vkCreatePipelineLayout(g.device,&layout_info,NULL,&layout));
   for(i=0;i<8;++i){sprintf(path,"%s/sample-%u.spv",argv[1],i);if(!shader(path,&modules[i]))goto done;
      pipeline_info.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
      pipeline_info.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
      pipeline_info.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;pipeline_info.stage.module=modules[i];
      pipeline_info.stage.pName="main";pipeline_info.layout=layout;
      OK(g.vkCreateComputePipelines(g.device,VK_NULL_HANDLE,1,&pipeline_info,NULL,&pipelines[i]));}
   sizes[0].type=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;sizes[0].descriptorCount=2;
   sizes[1].type=VK_DESCRIPTOR_TYPE_SAMPLER;sizes[1].descriptorCount=1;
   sizes[2].type=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;sizes[2].descriptorCount=1;
   pool_info.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;pool_info.maxSets=1;pool_info.poolSizeCount=3;pool_info.pPoolSizes=sizes;
   OK(g.vkCreateDescriptorPool(g.device,&pool_info,NULL,&descriptor_pool));
   set_allocate.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;set_allocate.descriptorPool=descriptor_pool;
   set_allocate.descriptorSetCount=1;set_allocate.pSetLayouts=&set_layout;
   OK(g.vkAllocateDescriptorSets(g.device,&set_allocate,&set));
   command_pool_info.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;command_pool_info.queueFamilyIndex=family;
   OK(g.vkCreateCommandPool(g.device,&command_pool_info,NULL,&command_pool));
   command_allocate.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;command_allocate.commandPool=command_pool;
   command_allocate.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;command_allocate.commandBufferCount=1;
   OK(g.vkAllocateCommandBuffers(g.device,&command_allocate,&command));
   fence_info.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;OK(g.vkCreateFence(g.device,&fence_info,NULL,&fence));
   begin.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
   submit.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;submit.commandBufferCount=1;submit.pCommandBuffers=&command;
   OK(g.vkBeginCommandBuffer(command,&begin));memset(image_barriers,0,sizeof(image_barriers));
   for(i=0;i<4;++i){image_barriers[i].sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      image_barriers[i].dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
      image_barriers[i].oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;image_barriers[i].newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      image_barriers[i].srcQueueFamilyIndex=image_barriers[i].dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
      image_barriers[i].image=images[i].image;image_barriers[i].subresourceRange=range;}
   g.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,4,image_barriers);
   for(i=0;i<4;++i){VkClearColorValue clear;memcpy(clear.float32,colors[i&1],sizeof(clear.float32));
      images_api.vkCmdClearColorImage(command,images[i].image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&clear,1,&range);
      image_barriers[i].srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;image_barriers[i].dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
      image_barriers[i].oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;image_barriers[i].newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;}
   g.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,NULL,0,NULL,4,image_barriers);
   OK(g.vkEndCommandBuffer(command));OK(g.vkQueueSubmit(queue,1,&submit,fence));
   if(g.vkWaitForFences(g.device,1,&fence,VK_TRUE,(uint64_t)1000000000*10)!=VK_SUCCESS)exit(1);
   for(which=0;which<8;++which)for(flags=0;flags<4;++flags)for(repeat=0;repeat<16;++repeat){
      VkDescriptorImageInfo image_infos[3];VkDescriptorBufferInfo buffer_info;
      VkWriteDescriptorSet writes[4];VkMemoryBarrier barrier={0};
      if((which&1) && flags!=1 && flags!=2)continue;
      constants[0]=(flags&1)?(1u<<repeat):0;constants[1]=(flags&2)?(1u<<(31-repeat)):0;
      for(i=0;i<2;++i)for(j=0;j<4;++j){sample=colors[i][j];first=i?-.25f:.75f;
         expected[i*4+j]=(flags&(1u<<i))?((which&4)?first+(sample-first)*.25f:sample):0;}
      memset(buffers[0].mapped,0xA5,4096);if(!cache_buffer(&buffers[0],0))goto done;
      memset(image_infos,0,sizeof(image_infos));memset(writes,0,sizeof(writes));
      for(i=0;i<2;++i){image_infos[i].imageView=images[(which&2)+i].view;image_infos[i].imageLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;}
      image_infos[2].sampler=sampler;buffer_info.buffer=buffers[0].buffer;buffer_info.offset=0;buffer_info.range=32;
      for(i=0;i<4;++i){writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[i].dstSet=set;
         writes[i].dstBinding=i;writes[i].descriptorCount=1;writes[i].descriptorType=bindings[i].descriptorType;
         if(i<3)writes[i].pImageInfo=&image_infos[i];else writes[i].pBufferInfo=&buffer_info;}
      g.vkUpdateDescriptorSets(g.device,4,writes,0,NULL);
      OK(g.vkResetCommandPool(g.device,command_pool,0));OK(g.vkBeginCommandBuffer(command,&begin));
      barrier.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;barrier.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_WRITE_BIT;
      g.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&barrier,0,NULL,0,NULL);
      g.vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipelines[which]);
      g.vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,NULL);
      g.vkCmdPushConstants(command,layout,VK_SHADER_STAGE_COMPUTE_BIT,0,8,constants);g.vkCmdDispatch(command,1,1,1);
      barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
      g.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,NULL,0,NULL);
      OK(g.vkEndCommandBuffer(command));OK(g.vkResetFences(g.device,1,&fence));OK(g.vkQueueSubmit(queue,1,&submit,fence));
      if(g.vkWaitForFences(g.device,1,&fence,VK_TRUE,(uint64_t)1000000000*10)!=VK_SUCCESS)exit(1);
      if(!cache_buffer(&buffers[0],1))goto done;
      if(memcmp(buffers[0].mapped,expected,32)){fprintf(stderr,"sampling mismatch: module%u flags%u repeat%u\n",which,flags,repeat);goto done;}
      for(byte=32;byte<4096;++byte)if(((unsigned char*)buffers[0].mapped)[byte]!=0xA5)goto done;
      ++cases;
   }
   printf("PASS: %u production sampling GPU cases, signed/unsigned/mixed,2D/3D, endpoint aliases and guards (%s)\n",cases,properties.deviceName);result=0;
done:
   if(g.device){
      if(fence)g.vkDestroyFence(g.device,fence,NULL);
      if(command_pool)g.vkDestroyCommandPool(g.device,command_pool,NULL);
      if(descriptor_pool)g.vkDestroyDescriptorPool(g.device,descriptor_pool,NULL);
      for(i=0;i<8;++i){if(pipelines[i])g.vkDestroyPipeline(g.device,pipelines[i],NULL);if(modules[i])g.vkDestroyShaderModule(g.device,modules[i],NULL);}
      if(layout)g.vkDestroyPipelineLayout(g.device,layout,NULL);
      if(set_layout)g.vkDestroyDescriptorSetLayout(g.device,set_layout,NULL);
      if(sampler)images_api.vkDestroySampler(g.device,sampler,NULL);
      for(i=0;i<4;++i){if(images[i].view)images_api.vkDestroyImageView(g.device,images[i].view,NULL);if(images[i].image)images_api.vkDestroyImage(g.device,images[i].image,NULL);if(images[i].memory)g.vkFreeMemory(g.device,images[i].memory,NULL);}
      close_buffer(&buffers[0]);g.vkDestroyDevice(g.device,NULL);
   }
   if(g.instance && g.vkDestroyInstance)g.vkDestroyInstance(g.instance,NULL);
#ifdef _WIN32
   if(loader)FreeLibrary(loader);
#else
   if(loader)dlclose(loader);
#endif
   return result;
}
