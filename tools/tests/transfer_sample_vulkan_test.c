/* One raw interpreter PSO, mutable instruction data, exact RTZ/FTZ oracles. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "vulkan_test_context.h"
#include "transfer_sample_cases.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"Sample GPU line %u\n",(unsigned)__LINE__);goto done;}}while(0)
enum {RECORDS=XE_SAMPLE_CASES,OUTPUT_RECORDS=XE_SAMPLE_OUTPUT_CASES};
static struct buffer input_buffer,output_buffer;
int main(int argc,char **argv){
#ifdef _WIN32
 HMODULE loader=LoadLibraryA("vulkan-1.dll");FARPROC entry;
#else
 void *loader=dlopen("libvulkan.so.1",RTLD_NOW|RTLD_LOCAL),*entry;
#endif
 static VkPhysicalDevice physical[8];static VkQueueFamilyProperties families[32];
 static VkPhysicalDeviceProperties properties;
 VkApplicationInfo app={0};VkInstanceCreateInfo instance={0};VkDeviceQueueCreateInfo queue_info={0};VkDeviceCreateInfo device_info={0};
 VkQueue queue=VK_NULL_HANDLE;VkDescriptorSetLayoutBinding bindings[2];VkDescriptorSetLayoutCreateInfo set_info={0};
 VkDescriptorSetLayout set_layout=VK_NULL_HANDLE;VkPipelineLayoutCreateInfo layout_info={0};VkPipelineLayout layout=VK_NULL_HANDLE;
 VkShaderModule module=VK_NULL_HANDLE;VkComputePipelineCreateInfo pipeline_info={0};VkPipeline pipeline=VK_NULL_HANDLE;
 VkDescriptorPoolSize pool_size={VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,2};VkDescriptorPoolCreateInfo pool_info={0};VkDescriptorPool pool=VK_NULL_HANDLE;
 VkDescriptorSetAllocateInfo set_allocate={0};VkDescriptorSet set=VK_NULL_HANDLE;VkDescriptorBufferInfo infos[2];VkWriteDescriptorSet writes[2];
 VkCommandPoolCreateInfo command_pool_info={0};VkCommandPool command_pool=VK_NULL_HANDLE;VkCommandBufferAllocateInfo command_allocate={0};
 VkCommandBuffer command;VkCommandBufferBeginInfo begin={0};VkMemoryBarrier barrier={0};VkFenceCreateInfo fence_info={0};VkFence fence=VK_NULL_HANDLE;VkSubmitInfo submit={0};
 uint32_t count,family,i,lane,round,limit,*in,*out,wanted[7];
 unsigned modules=0,pipelines=0,cases=0;float priority=1.0f;int result=1;
 CHECK(loader && argc==2);
#ifdef _WIN32
 entry=GetProcAddress(loader,"vkGetInstanceProcAddr");
#else
 entry=dlsym(loader,"vkGetInstanceProcAddr");
#endif
 memcpy(&g.get,&entry,sizeof(g.get));CHECK(g.get);LOAD(vkCreateInstance);
 app.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO;app.apiVersion=VK_API_VERSION_1_0;
 instance.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;instance.pApplicationInfo=&app;OK(g.vkCreateInstance(&instance,NULL,&g.instance));
 LOAD(vkDestroyInstance);LOAD(vkEnumeratePhysicalDevices);LOAD(vkGetPhysicalDeviceProperties);LOAD(vkGetPhysicalDeviceMemoryProperties);LOAD(vkGetPhysicalDeviceQueueFamilyProperties);LOAD(vkCreateDevice);
 count=8;OK(g.vkEnumeratePhysicalDevices(g.instance,&count,physical));CHECK(count);g.vkGetPhysicalDeviceProperties(physical[0],&properties);
 fprintf(stderr,"SAMPLE_DEVICE vendor=%u device=%u driver=%u name=%s\n",properties.vendorID,properties.deviceID,properties.driverVersion,properties.deviceName);
 g.vkGetPhysicalDeviceMemoryProperties(physical[0],&g.memory);count=32;g.vkGetPhysicalDeviceQueueFamilyProperties(physical[0],&count,families);
 for(family=0;family<count;++family){if(families[family].queueFlags&VK_QUEUE_COMPUTE_BIT)break;}
 CHECK(family<count);
 queue_info.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;queue_info.queueFamilyIndex=family;queue_info.queueCount=1;queue_info.pQueuePriorities=&priority;
 device_info.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;device_info.queueCreateInfoCount=1;device_info.pQueueCreateInfos=&queue_info;OK(g.vkCreateDevice(physical[0],&device_info,NULL,&g.device));
 LOAD(vkDestroyDevice);LOAD(vkGetDeviceQueue);LOAD(vkCreateBuffer);LOAD(vkDestroyBuffer);LOAD(vkGetBufferMemoryRequirements);LOAD(vkAllocateMemory);LOAD(vkFreeMemory);
 LOAD(vkBindBufferMemory);LOAD(vkMapMemory);LOAD(vkUnmapMemory);LOAD(vkFlushMappedMemoryRanges);LOAD(vkInvalidateMappedMemoryRanges);
 LOAD(vkCreateDescriptorSetLayout);LOAD(vkDestroyDescriptorSetLayout);LOAD(vkCreatePipelineLayout);LOAD(vkDestroyPipelineLayout);LOAD(vkCreateShaderModule);LOAD(vkDestroyShaderModule);
 LOAD(vkCreateComputePipelines);LOAD(vkDestroyPipeline);LOAD(vkCreateDescriptorPool);LOAD(vkDestroyDescriptorPool);LOAD(vkAllocateDescriptorSets);LOAD(vkUpdateDescriptorSets);
 LOAD(vkCreateCommandPool);LOAD(vkDestroyCommandPool);LOAD(vkAllocateCommandBuffers);LOAD(vkResetCommandPool);LOAD(vkBeginCommandBuffer);LOAD(vkEndCommandBuffer);
 LOAD(vkCmdPipelineBarrier);LOAD(vkCmdBindPipeline);LOAD(vkCmdBindDescriptorSets);LOAD(vkCmdDispatch);LOAD(vkCreateFence);LOAD(vkDestroyFence);LOAD(vkResetFences);LOAD(vkQueueSubmit);LOAD(vkWaitForFences);
 g.vkGetDeviceQueue(g.device,family,0,&queue);CHECK(make_buffer(&input_buffer,RECORDS*128) && make_buffer(&output_buffer,OUTPUT_RECORDS*64));
 memset(bindings,0,sizeof(bindings));for(i=0;i<2;++i){bindings[i].binding=i;bindings[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;bindings[i].descriptorCount=1;bindings[i].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;}
 set_info.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;set_info.bindingCount=2;set_info.pBindings=bindings;OK(g.vkCreateDescriptorSetLayout(g.device,&set_info,NULL,&set_layout));
 layout_info.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;layout_info.setLayoutCount=1;layout_info.pSetLayouts=&set_layout;OK(g.vkCreatePipelineLayout(g.device,&layout_info,NULL,&layout));
 CHECK(shader(argv[1],&module));++modules;
 pipeline_info.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;pipeline_info.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
 pipeline_info.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;pipeline_info.stage.module=module;pipeline_info.stage.pName="main";pipeline_info.layout=layout;
 ++pipelines;OK(g.vkCreateComputePipelines(g.device,VK_NULL_HANDLE,1,&pipeline_info,NULL,&pipeline));
 pool_info.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;pool_info.maxSets=1;pool_info.poolSizeCount=1;pool_info.pPoolSizes=&pool_size;OK(g.vkCreateDescriptorPool(g.device,&pool_info,NULL,&pool));
 set_allocate.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;set_allocate.descriptorPool=pool;set_allocate.descriptorSetCount=1;set_allocate.pSetLayouts=&set_layout;OK(g.vkAllocateDescriptorSets(g.device,&set_allocate,&set));
 command_pool_info.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;command_pool_info.queueFamilyIndex=family;OK(g.vkCreateCommandPool(g.device,&command_pool_info,NULL,&command_pool));
 command_allocate.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;command_allocate.commandPool=command_pool;command_allocate.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;command_allocate.commandBufferCount=1;OK(g.vkAllocateCommandBuffers(g.device,&command_allocate,&command));
 fence_info.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;OK(g.vkCreateFence(g.device,&fence_info,NULL,&fence));
 in=(uint32_t*)input_buffer.mapped;out=(uint32_t*)output_buffer.mapped;
 for(round=0;round<3;++round){limit=round==1?RECORDS-17:RECORDS;
  for(i=0;i<RECORDS;++i)xe_sample_input(in+i*32,i,round);
  memset(out,0x6d,OUTPUT_RECORDS*64);CHECK(cache_buffer(&input_buffer,0) && cache_buffer(&output_buffer,0));
  memset(infos,0,sizeof(infos));infos[0].buffer=input_buffer.buffer;infos[0].range=limit*128;infos[1].buffer=output_buffer.buffer;infos[1].range=OUTPUT_RECORDS*64;
  memset(writes,0,sizeof(writes));for(i=0;i<2;++i){writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[i].dstSet=set;writes[i].dstBinding=i;writes[i].descriptorCount=1;writes[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[i].pBufferInfo=&infos[i];}
  g.vkUpdateDescriptorSets(g.device,2,writes,0,NULL);OK(g.vkResetCommandPool(g.device,command_pool,0));
  begin.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;OK(g.vkBeginCommandBuffer(command,&begin));
  barrier.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;barrier.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
  g.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&barrier,0,NULL,0,NULL);
  g.vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);g.vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,NULL);g.vkCmdDispatch(command,(OUTPUT_RECORDS+63)/64+1,1,1);
  barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;g.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,NULL,0,NULL);
  OK(g.vkEndCommandBuffer(command));OK(g.vkResetFences(g.device,1,&fence));submit.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;submit.commandBufferCount=1;submit.pCommandBuffers=&command;
  OK(g.vkQueueSubmit(queue,1,&submit,fence));if(g.vkWaitForFences(g.device,1,&fence,VK_TRUE,UINT64_C(5000000000))!=VK_SUCCESS){fprintf(stderr,"GPU timeout: retaining pending test resources\n");exit(1);}CHECK(cache_buffer(&output_buffer,1));
  for(i=0;i<OUTPUT_RECORDS;++i) {
   if(i>=limit) {for(lane=0;lane<16;++lane)CHECK(out[i*16+lane]==0x6d6d6d6du);continue;}
   xe_sample_expected(in+i*32,wanted);
   for(lane=0;lane<7;++lane)if(out[i*16+lane]!=wanted[lane]) {
    fprintf(stderr,"Sample round %u case %u lane %u: %08x != %08x\n",round,i,lane,out[i*16+lane],wanted[lane]);goto done;
   }
   for(lane=7;lane<16;++lane)CHECK(out[i*16+lane]==0x6d6d6d6du);
   ++cases;
  }
 }
 CHECK(modules==1 && pipelines==1);
 printf("PASS: %u dynamic sample records, all scale/MSAA layouts, independent inverse inputs, bounds/padding guards; one module and PSO\n",cases);result=0;

done:
 if(g.device){if(fence)g.vkDestroyFence(g.device,fence,NULL);
  if(command_pool)g.vkDestroyCommandPool(g.device,command_pool,NULL);
  if(pool)g.vkDestroyDescriptorPool(g.device,pool,NULL);
  if(pipeline)g.vkDestroyPipeline(g.device,pipeline,NULL);
  if(module)g.vkDestroyShaderModule(g.device,module,NULL);
  if(layout)g.vkDestroyPipelineLayout(g.device,layout,NULL);
  if(set_layout)g.vkDestroyDescriptorSetLayout(g.device,set_layout,NULL);
  close_buffer(&output_buffer);close_buffer(&input_buffer);g.vkDestroyDevice(g.device,NULL);}
 if(g.instance)g.vkDestroyInstance(g.instance,NULL);

#ifdef _WIN32
 if(loader)FreeLibrary(loader);
#else
 if(loader)dlclose(loader);
#endif
 return result;}
