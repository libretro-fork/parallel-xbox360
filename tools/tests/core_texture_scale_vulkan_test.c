/* Componentwise conversion differential and independent goldens. C89. */
#include "vulkan_test_context.h"
/* Independent scalar model for these finite, bounded test inputs. Volatile
 * intermediates force binary32 rounding without a contracted multiply-add. */
static float multiply(float a,float b) { volatile float out=a*b;return out; }
static float add(float a,float b) { volatile float out=a+b;return out; }
static float divide(float a,float b) { volatile float out=a/b;return out; }
static float round_even(float value)
{
   long base=(long)value;
   double fraction;
   if((double)value<(double)base)--base;
   fraction=(double)value-(double)base;
   if(fraction>.5 || (fraction==.5 && base%2))++base;
   return (float)base;
}
static float scalar_reference(float value,uint32_t packed,unsigned lane,unsigned point)
{
   unsigned field=(packed>>(lane*6))&63,sign=field>>4,width=field&15,shift;
   float power,texel,scale,offset;
   if(!(packed&((UINT32_C(1)<<26)-1)))return value;
   if(packed&(UINT32_C(1)<<24)) {
      if(point && (packed&((UINT32_C(1)<<24)-1)) && field>=1 && field<=15) {
         power=(float)(2u<<width);
         texel=round_even(multiply(value,add(power,-1.0f)));
         value=divide(multiply(texel,add(power,1.0f)),multiply(power,power));
      }
      if(!sign)value=multiply(round_even(multiply(value,65536.0f)),1.0f/65536.0f);
      if(sign==2 && value<-1.0f)value=-1.0f;
      return value;
   }
   shift=width+1-((sign==1 || sign==2)?1:0);
   scale=(float)((1u<<shift)-1);
   offset=add(scale<.5f?scale:.5f,-.5f);
   if(scale<.5f)scale=.5f;
   value=add(multiply(value,scale),offset);
   if(point && (packed&(UINT32_C(1)<<26)))value=round_even(value);
   return value;
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
   VkDescriptorSetLayoutBinding binding={0};VkDescriptorSetLayoutCreateInfo set_info={0};
   VkDescriptorSetLayout set_layout=VK_NULL_HANDLE,set_layouts[2];VkPipelineLayoutCreateInfo layout_info={0};
   VkPushConstantRange push;VkPipelineLayout layout=VK_NULL_HANDLE;
   VkShaderModule modules[30]={0};VkPipeline pipelines[30]={0};VkComputePipelineCreateInfo pipeline_info={0};
   VkDescriptorPoolSize pool_size;VkDescriptorPoolCreateInfo pool_info={0};
   VkDescriptorPool descriptor_pool=VK_NULL_HANDLE;VkDescriptorSet sets[2];
   VkDescriptorSetAllocateInfo set_allocate={0};
   VkCommandPoolCreateInfo command_pool_info={0};VkCommandPool command_pool=VK_NULL_HANDLE;
   VkCommandBufferAllocateInfo command_allocate={0};VkCommandBuffer command;
   VkCommandBufferBeginInfo begin={0};VkFenceCreateInfo fence_info={0};VkFence fence=VK_NULL_HANDLE;
   VkSubmitInfo submit={0};struct buffer buffers[2];float priority=1.0f;
   const float samples[3][4]={{.125f,.5f,.75f,1},{-1.25f,-.25f,.25f,.875f},{.1234567f,-.54321f,.0001f,1.999f}};
   uint32_t count,family,i,lane,mask,point,sign,width,normalized,snap,pattern,constants[5],cases=0,packed,known;
   uint32_t *reference,*compact,bits,checks=0;float known_input[4]={8.0f/15,.5f,-.25f,.125f};
   size_t byte;int result=1;char path[512];float input_value,observed,golden;
   memset(buffers,0,sizeof(buffers));
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

   if(!make_buffer(&buffers[0],4096) || !make_buffer(&buffers[1],4096))goto done;
   binding.binding=0;binding.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;binding.descriptorCount=1;binding.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;
   set_info.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;set_info.bindingCount=1;set_info.pBindings=&binding;
   OK(g.vkCreateDescriptorSetLayout(g.device,&set_info,NULL,&set_layout));
   push.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;push.offset=0;push.size=20;
   layout_info.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;layout_info.setLayoutCount=1;
   layout_info.pSetLayouts=&set_layout;layout_info.pushConstantRangeCount=1;layout_info.pPushConstantRanges=&push;
   OK(g.vkCreatePipelineLayout(g.device,&layout_info,NULL,&layout));
   for(mask=1;mask<=15;++mask)for(point=0;point<2;++point){i=(mask-1)*2+point;
      sprintf(path,"%s/scale-%u-%u.spv",argv[1],mask,point);if(!shader(path,&modules[i]))goto done;
      pipeline_info.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
      pipeline_info.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
      pipeline_info.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;pipeline_info.stage.module=modules[i];
      pipeline_info.stage.pName="main";pipeline_info.layout=layout;
      OK(g.vkCreateComputePipelines(g.device,VK_NULL_HANDLE,1,&pipeline_info,NULL,&pipelines[i]));}
   pool_size.type=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;pool_size.descriptorCount=2;
   pool_info.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;pool_info.maxSets=2;pool_info.poolSizeCount=1;pool_info.pPoolSizes=&pool_size;
   OK(g.vkCreateDescriptorPool(g.device,&pool_info,NULL,&descriptor_pool));
   set_layouts[0]=set_layouts[1]=set_layout;
   set_allocate.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;set_allocate.descriptorPool=descriptor_pool;
   set_allocate.descriptorSetCount=2;set_allocate.pSetLayouts=set_layouts;
   OK(g.vkAllocateDescriptorSets(g.device,&set_allocate,sets));
   for(i=0;i<2;++i){VkDescriptorBufferInfo info;VkWriteDescriptorSet write={0};
      info.buffer=buffers[i].buffer;info.offset=0;info.range=16;
      write.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;write.dstSet=sets[i];write.dstBinding=0;
      write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;write.pBufferInfo=&info;
      g.vkUpdateDescriptorSets(g.device,1,&write,0,NULL);}
   command_pool_info.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;command_pool_info.queueFamilyIndex=family;
   OK(g.vkCreateCommandPool(g.device,&command_pool_info,NULL,&command_pool));
   command_allocate.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;command_allocate.commandPool=command_pool;
   command_allocate.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;command_allocate.commandBufferCount=1;
   OK(g.vkAllocateCommandBuffers(g.device,&command_allocate,&command));
   fence_info.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;OK(g.vkCreateFence(g.device,&fence_info,NULL,&fence));
   begin.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
   submit.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;submit.commandBufferCount=1;submit.pCommandBuffers=&command;
   for(mask=1;mask<=15;++mask)for(point=0;point<2;++point)for(sign=0;sign<4;++sign)
   for(width=0;width<16;++width)for(normalized=0;normalized<2;++normalized)
   for(snap=0;snap<2;++snap)for(pattern=0;pattern<3;++pattern) {
      known=0;packed=(normalized<<24)|(snap<<26);
      for(lane=0;lane<4;++lane)packed|=(((width+lane*3)&15)|(((sign+lane)&3)<<4))<<(lane*6);
      memcpy(constants+1,samples[pattern],16);
      /* Independent bypass and4-bit point reconstruction goldens are inserted
       * without adding another dispatch loop. All masks containing R check R. */
      if(sign==0 && width==0 && normalized==0 && snap==0 && pattern==0){packed=0;known=1;}
      if(point && sign==0 && width==0 && normalized && snap && pattern==0){packed=3u|(1u<<24)|(1u<<26);known=2;memcpy(constants+1,known_input,16);}
      constants[0]=packed;
      for(i=0;i<2;++i){memset(buffers[i].mapped,0xA5,4096);if(!cache_buffer(&buffers[i],0))goto done;}
      OK(g.vkResetCommandPool(g.device,command_pool,0));OK(g.vkBeginCommandBuffer(command,&begin));
      {VkMemoryBarrier barrier={0};barrier.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;
       barrier.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_WRITE_BIT;
       g.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&barrier,0,NULL,0,NULL);}
      for(i=0;i<2;++i){g.vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipelines[i?(mask-1)*2+point:28+point]);
         g.vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&sets[i],0,NULL);
         g.vkCmdPushConstants(command,layout,VK_SHADER_STAGE_COMPUTE_BIT,0,20,constants);g.vkCmdDispatch(command,1,1,1);}
      {VkMemoryBarrier barrier={0};barrier.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;
       barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
       g.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,NULL,0,NULL);}
      OK(g.vkEndCommandBuffer(command));OK(g.vkResetFences(g.device,1,&fence));OK(g.vkQueueSubmit(queue,1,&submit,fence));
      if(g.vkWaitForFences(g.device,1,&fence,VK_TRUE,(uint64_t)1000000000*10)!=VK_SUCCESS)exit(1);
      if(!cache_buffer(&buffers[0],1) || !cache_buffer(&buffers[1],1))goto done;
      reference=(uint32_t*)buffers[0].mapped;compact=(uint32_t*)buffers[1].mapped;
      for(lane=0;lane<4;++lane){bits=(mask&(1u<<lane))?reference[lane]:constants[lane+1];
         if(compact[lane]!=bits){fprintf(stderr,"scale mismatch mask%u point%u sign%u width%u norm%u snap%u pattern%u lane%u: %08x != %08x\n",mask,point,sign,width,normalized,snap,pattern,lane,compact[lane],bits);goto done;}
         if(known==1 && reference[lane]!=constants[lane+1])goto done;
         memcpy(&input_value,constants+lane+1,4);memcpy(&observed,reference+lane,4);
         golden=scalar_reference(input_value,packed,lane,point);
         /* Numeric equality admits either zero sign in the scalar model.
          * The differential above independently requires exact legacy bits. */
         if(observed!=golden){fprintf(stderr,"scalar golden mismatch mask%u lane%u packed%08x: %.9g != %.9g\n",mask,lane,packed,(double)observed,(double)golden);goto done;}
         ++checks;}
      if(known==2 && reference[0]!=UINT32_C(0x3f080000)){fprintf(stderr,"4-bit reconstruction golden failed\n");goto done;}
      for(i=0;i<2;++i)for(byte=16;byte<4096;++byte)if(((unsigned char*)buffers[i].mapped)[byte]!=0xA5)goto done;
      ++cases;
   }
   printf("PASS: %u conversion GPU differentials/%u lane checks; masks1-15, width-fields0-15, all signs, normalized/integer, point/filter and guards (%s)\n",cases,checks,properties.deviceName);result=0;
done:
   if(g.device){
      if(fence)g.vkDestroyFence(g.device,fence,NULL);
      if(command_pool)g.vkDestroyCommandPool(g.device,command_pool,NULL);
      if(descriptor_pool)g.vkDestroyDescriptorPool(g.device,descriptor_pool,NULL);
      for(i=0;i<30;++i){if(pipelines[i])g.vkDestroyPipeline(g.device,pipelines[i],NULL);if(modules[i])g.vkDestroyShaderModule(g.device,modules[i],NULL);}
      if(layout)g.vkDestroyPipelineLayout(g.device,layout,NULL);
      if(set_layout)g.vkDestroyDescriptorSetLayout(g.device,set_layout,NULL);
      for(i=0;i<2;++i)close_buffer(&buffers[i]);
      g.vkDestroyDevice(g.device,NULL);
   }
   if(g.instance && g.vkDestroyInstance)g.vkDestroyInstance(g.instance,NULL);
#ifdef _WIN32
   if(loader)FreeLibrary(loader);
#else
   if(loader)dlclose(loader);
#endif
   return result;
}
