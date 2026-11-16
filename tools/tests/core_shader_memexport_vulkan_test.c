/* Actual guest vertex memory-export writes and complete SSBO guards. C89.
 * Derived from the existing call GPU fixture; no transform-feedback extension
 * or release dependency. Uses the portable software-FP profile, retaining native-profile module tests separately. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#define XE_VK_TEST_GRAPHICS
#define XE_VK_TEST_NO_XFB
#include "vulkan_test_context.h"
#include "shader_compile_bench.h"
#ifndef XE_VERTEX_TEST_CASES
#define XE_VERTEX_TEST_CASES 4
#endif
int main(int argc,char **argv) {
#ifdef _WIN32
 HMODULE loader=LoadLibraryA("vulkan-1.dll");FARPROC entry;
#else
 void *loader=dlopen("libvulkan.so.1",RTLD_NOW|RTLD_LOCAL),*entry;
#endif
 VkApplicationInfo app={0};VkInstanceCreateInfo instance_info={0};
 VkPhysicalDevice physical[8];VkPhysicalDeviceProperties properties;
 VkPhysicalDeviceFeatures2 features={0};VkPhysicalDeviceFeatures enabled={0};
 VkPhysicalDeviceMaintenance5FeaturesKHR maintenance5={0};
 const char *extension=VK_KHR_MAINTENANCE_5_EXTENSION_NAME;
 VkQueueFamilyProperties families[32];VkDeviceQueueCreateInfo queue_info={0};
 VkDeviceCreateInfo device_info={0};VkQueue queue=VK_NULL_HANDLE;
 VkDescriptorSetLayoutBinding bindings[4];VkDescriptorSetLayoutCreateInfo set_info={0};
 VkDescriptorSetLayout layouts[2]={0};VkPipelineLayoutCreateInfo layout_info={0};
 VkPipelineLayout layout=VK_NULL_HANDLE;VkShaderModule module=VK_NULL_HANDLE;VkPipeline pipeline=VK_NULL_HANDLE;
 VkPipelineShaderStageCreateInfo stage={0};VkPipelineVertexInputStateCreateInfo vertex={0};
 VkPipelineInputAssemblyStateCreateInfo assembly={0};VkPipelineViewportStateCreateInfo viewport_state={0};
 VkPipelineRasterizationStateCreateInfo raster={0};VkPipelineMultisampleStateCreateInfo multisample={0};
 VkGraphicsPipelineCreateInfo pipeline_info={0};VkViewport viewport={0};VkRect2D scissor={{0,0},{1,1}};
 VkSubpassDescription subpass={0};VkRenderPassCreateInfo render_info={0};VkRenderPass render=VK_NULL_HANDLE;
 VkFramebufferCreateInfo framebuffer_info={0};VkFramebuffer framebuffer=VK_NULL_HANDLE;
 VkDescriptorPoolSize pool_sizes[2];VkDescriptorPoolCreateInfo pool_info={0};
 VkDescriptorPool pool=VK_NULL_HANDLE;VkDescriptorSet sets[2];VkDescriptorSetAllocateInfo allocate_sets={0};
 VkDescriptorBufferInfo infos[5];VkWriteDescriptorSet writes[5];
 VkCommandPoolCreateInfo command_info={0};VkCommandPool commands=VK_NULL_HANDLE;
 VkCommandBufferAllocateInfo allocate_command={0};VkCommandBuffer command;
 VkCommandBufferBeginInfo begin={0};VkRenderPassBeginInfo render_begin={0};
 VkFenceCreateInfo fence_info={0};VkFence fence=VK_NULL_HANDLE;VkSubmitInfo submit={0};
 VkMemoryBarrier barrier={0};struct buffer buffers[2];
 float priority=1.0f;uint32_t count,family,i,which,expected,actual,*constants;char path[512];
 static const char *names[4]={"portable-linear-s0-m1.spv","portable-linear-s0-m31.spv","portable-fallthrough-s0.spv","portable-pending-branch-s0.spv"};
 int missing_store=argc==3 && !strcmp(argv[2],"--missing-store");
 int result=1,validation=getenv("XE_MEMEXPORT_GPU_VALIDATION")!=NULL;
 const char *validation_layer="VK_LAYER_KHRONOS_validation";
 memset(buffers,0,sizeof(buffers));
 if((argc!=2 && !missing_store) || strlen(argv[1])>400 || !loader)goto done;
#ifdef _WIN32
 entry=GetProcAddress(loader,"vkGetInstanceProcAddr");
#else
 entry=dlsym(loader,"vkGetInstanceProcAddr");
#endif
 memcpy(&g.get,&entry,sizeof(g.get));if(!g.get)goto done;
 LOAD(vkCreateInstance);
 app.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO;app.apiVersion=VK_MAKE_API_VERSION(0,1,3,0);
 instance_info.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;instance_info.pApplicationInfo=&app;
 if(validation){instance_info.enabledLayerCount=1;instance_info.ppEnabledLayerNames=&validation_layer;}
 OK(g.vkCreateInstance(&instance_info,NULL,&g.instance));
 LOAD(vkDestroyInstance);LOAD(vkEnumeratePhysicalDevices);LOAD(vkGetPhysicalDeviceProperties);
 LOAD(vkGetPhysicalDeviceMemoryProperties);LOAD(vkGetPhysicalDeviceQueueFamilyProperties);
 LOAD(vkGetPhysicalDeviceFeatures2);LOAD(vkCreateDevice);
 count=8;OK(g.vkEnumeratePhysicalDevices(g.instance,&count,physical));if(!count)goto done;
 g.vkGetPhysicalDeviceProperties(physical[0],&properties);
 fprintf(stderr,"BENCH_DEVICE Vulkan vendor=%u device=%u driver=%u name=%s\n",properties.vendorID,properties.deviceID,properties.driverVersion,properties.deviceName);
 g.vkGetPhysicalDeviceMemoryProperties(physical[0],&g.memory);
 maintenance5.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_FEATURES_KHR;
 features.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;features.pNext=&maintenance5;
 g.vkGetPhysicalDeviceFeatures2(physical[0],&features);
 if(!features.features.vertexPipelineStoresAndAtomics){fputs("vertex stores unsupported\n",stderr);goto done;}
 if(!maintenance5.maintenance5){fputs("maintenance5 point-size exemption unsupported (not a pass)\n",stderr);goto done;}
 enabled.vertexPipelineStoresAndAtomics=VK_TRUE;
 count=32;g.vkGetPhysicalDeviceQueueFamilyProperties(physical[0],&count,families);
 for(family=0;family<count;++family)if(families[family].queueFlags&VK_QUEUE_GRAPHICS_BIT)break;
 if(family==count)goto done;
 queue_info.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;queue_info.queueFamilyIndex=family;
 queue_info.queueCount=1;queue_info.pQueuePriorities=&priority;
 device_info.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;device_info.pEnabledFeatures=&enabled;device_info.pNext=&maintenance5;
 device_info.enabledExtensionCount=1;device_info.ppEnabledExtensionNames=&extension;
 device_info.queueCreateInfoCount=1;device_info.pQueueCreateInfos=&queue_info;
 OK(g.vkCreateDevice(physical[0],&device_info,NULL,&g.device));
 LOAD(vkDestroyDevice);LOAD(vkGetDeviceQueue);LOAD(vkCreateBuffer);LOAD(vkDestroyBuffer);
 LOAD(vkGetBufferMemoryRequirements);LOAD(vkAllocateMemory);LOAD(vkFreeMemory);
 LOAD(vkBindBufferMemory);LOAD(vkMapMemory);LOAD(vkUnmapMemory);
 LOAD(vkFlushMappedMemoryRanges);LOAD(vkInvalidateMappedMemoryRanges);
 LOAD(vkCreateShaderModule);LOAD(vkDestroyShaderModule);
 LOAD(vkCreateDescriptorSetLayout);LOAD(vkDestroyDescriptorSetLayout);
 LOAD(vkCreatePipelineLayout);LOAD(vkDestroyPipelineLayout);LOAD(vkCreateGraphicsPipelines);LOAD(vkDestroyPipeline);
 LOAD(vkCreateRenderPass);LOAD(vkDestroyRenderPass);LOAD(vkCreateFramebuffer);LOAD(vkDestroyFramebuffer);
 LOAD(vkCreateDescriptorPool);LOAD(vkDestroyDescriptorPool);LOAD(vkAllocateDescriptorSets);LOAD(vkUpdateDescriptorSets);
 LOAD(vkCreateCommandPool);LOAD(vkDestroyCommandPool);LOAD(vkAllocateCommandBuffers);LOAD(vkResetCommandPool);
 LOAD(vkBeginCommandBuffer);LOAD(vkEndCommandBuffer);LOAD(vkCmdPipelineBarrier);LOAD(vkCmdBindPipeline);
 LOAD(vkCmdBindDescriptorSets);LOAD(vkCmdBeginRenderPass);LOAD(vkCmdEndRenderPass);LOAD(vkCmdDraw);
 LOAD(vkCreateFence);LOAD(vkDestroyFence);LOAD(vkResetFences);LOAD(vkQueueSubmit);LOAD(vkWaitForFences);
 g.vkGetDeviceQueue(g.device,family,0,&queue);
 for(i=0;i<2;++i)if(!make_buffer(&buffers[i],4096))goto done;
 memset(buffers[0].mapped,0,4096);memset(buffers[1].mapped,0,4096);
 ((uint32_t*)buffers[0].mapped)[0]=32; /* guest W is W, not reciprocal W. */
 for(i=8;i<11;++i)((float*)buffers[0].mapped)[i]=1.0f; /* NDC scale. */
 constants=(uint32_t*)((unsigned char*)buffers[0].mapped+1024);
 constants[4]=UINT32_C(0x40000001);constants[5]=UINT32_C(0x4b000000);
 constants[6]=UINT32_C(0x4b000600);constants[7]=UINT32_C(0x4b000006);
 if(!cache_buffer(&buffers[0],0) || !cache_buffer(&buffers[1],0))goto done;
 memset(bindings,0,sizeof(bindings));bindings[0].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
 bindings[0].descriptorCount=1;bindings[0].stageFlags=VK_SHADER_STAGE_VERTEX_BIT;
 set_info.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;set_info.bindingCount=1;set_info.pBindings=bindings;
 OK(g.vkCreateDescriptorSetLayout(g.device,&set_info,NULL,&layouts[0]));
 for(i=0;i<4;++i){bindings[i].binding=i<2?i:i+1;bindings[i].descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  bindings[i].descriptorCount=1;bindings[i].stageFlags=VK_SHADER_STAGE_VERTEX_BIT;}
 set_info.bindingCount=4;OK(g.vkCreateDescriptorSetLayout(g.device,&set_info,NULL,&layouts[1]));
 layout_info.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;layout_info.setLayoutCount=2;layout_info.pSetLayouts=layouts;
 OK(g.vkCreatePipelineLayout(g.device,&layout_info,NULL,&layout));
 subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;
 render_info.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;render_info.subpassCount=1;render_info.pSubpasses=&subpass;
 OK(g.vkCreateRenderPass(g.device,&render_info,NULL,&render));
 framebuffer_info.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;framebuffer_info.renderPass=render;
 framebuffer_info.width=framebuffer_info.height=framebuffer_info.layers=1;
 OK(g.vkCreateFramebuffer(g.device,&framebuffer_info,NULL,&framebuffer));
 pool_sizes[0].type=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;pool_sizes[0].descriptorCount=4;
 pool_sizes[1].type=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;pool_sizes[1].descriptorCount=1;
 pool_info.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;pool_info.maxSets=2;pool_info.poolSizeCount=2;pool_info.pPoolSizes=pool_sizes;
 OK(g.vkCreateDescriptorPool(g.device,&pool_info,NULL,&pool));
 allocate_sets.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;allocate_sets.descriptorPool=pool;
 allocate_sets.descriptorSetCount=2;allocate_sets.pSetLayouts=layouts;
 OK(g.vkAllocateDescriptorSets(g.device,&allocate_sets,sets));
 memset(infos,0,sizeof(infos));memset(writes,0,sizeof(writes));
 for(i=0;i<5;++i){
  infos[i].buffer=buffers[i?0:1].buffer;infos[i].offset=i?(i-1)*1024:0;infos[i].range=i?1024:4096;
  writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[i].dstSet=sets[i?1:0];
  writes[i].dstBinding=i?(i<=2?i-1:i):0;writes[i].descriptorCount=1;
  writes[i].descriptorType=i?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[i].pBufferInfo=&infos[i];
 }
 g.vkUpdateDescriptorSets(g.device,5,writes,0,NULL);
 command_info.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;command_info.queueFamilyIndex=family;
 OK(g.vkCreateCommandPool(g.device,&command_info,NULL,&commands));
 allocate_command.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;allocate_command.commandPool=commands;
 allocate_command.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;allocate_command.commandBufferCount=1;
 OK(g.vkAllocateCommandBuffers(g.device,&allocate_command,&command));
 fence_info.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;OK(g.vkCreateFence(g.device,&fence_info,NULL,&fence));
 stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;stage.stage=VK_SHADER_STAGE_VERTEX_BIT;stage.pName="main";
 vertex.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
 assembly.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;assembly.topology=VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
 viewport.width=viewport.height=1.0f;viewport.maxDepth=1.0f;
 viewport_state.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;viewport_state.viewportCount=1;
 viewport_state.pViewports=&viewport;viewport_state.scissorCount=1;viewport_state.pScissors=&scissor;
 raster.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;raster.rasterizerDiscardEnable=VK_TRUE;raster.lineWidth=1.0f;
 multisample.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;multisample.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
 pipeline_info.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;pipeline_info.stageCount=1;pipeline_info.pStages=&stage;
 pipeline_info.pVertexInputState=&vertex;pipeline_info.pInputAssemblyState=&assembly;pipeline_info.pViewportState=&viewport_state;
 pipeline_info.pRasterizationState=&raster;pipeline_info.pMultisampleState=&multisample;pipeline_info.layout=layout;pipeline_info.renderPass=render;
 begin.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;submit.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;submit.commandBufferCount=1;submit.pCommandBuffers=&command;
 render_begin.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;render_begin.renderPass=render;render_begin.framebuffer=framebuffer;
 render_begin.renderArea.extent.width=render_begin.renderArea.extent.height=1;
 barrier.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;
 for(which=0;which<XE_VERTEX_TEST_CASES;++which){
  /* c0=zero and c1 is eA. Every executed eM is zero at vertex ID zero.
   * The conditional branch is taken; its alternate loop and eM1 writer are
   * not executed. Final mask summary must still retain the pending eM0 path. */
  ((uint32_t*)((unsigned char*)buffers[0].mapped+2048))[0]=0;
  if(!cache_buffer(&buffers[0],0))goto done;
  sprintf(path,"%s/%s",argv[1],missing_store && !which?"portable-linear-s0-m0.spv":names[which]);
  if(!shader(path,&module))goto done;
  stage.module=module;XE_COMPILE_BEGIN();
  OK(g.vkCreateGraphicsPipelines(g.device,VK_NULL_HANDLE,1,&pipeline_info,NULL,&pipeline));
  XE_COMPILE_END("vulkan.graphics_pso",which);
  memset(buffers[1].mapped,0xA5,4096);if(!cache_buffer(&buffers[1],0))goto done;
  OK(g.vkResetCommandPool(g.device,commands,0));OK(g.vkBeginCommandBuffer(command,&begin));
  barrier.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_UNIFORM_READ_BIT|VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
  g.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,0,1,&barrier,0,NULL,0,NULL);
  g.vkCmdBeginRenderPass(command,&render_begin,VK_SUBPASS_CONTENTS_INLINE);
  g.vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);
  g.vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,0,2,sets,0,NULL);
  g.vkCmdDraw(command,1,1,0,0);g.vkCmdEndRenderPass(command);
  barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
  g.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,NULL,0,NULL);
  OK(g.vkEndCommandBuffer(command));OK(g.vkResetFences(g.device,1,&fence));OK(g.vkQueueSubmit(queue,1,&submit,fence));
  if(g.vkWaitForFences(g.device,1,&fence,VK_TRUE,UINT64_C(10000000000))!=VK_SUCCESS){fprintf(stderr,"GPU completion failed, retaining pending resources\n");exit(1);}
  if(!cache_buffer(&buffers[1],1))goto done;
  for(i=0;i<1024;++i){
   expected=i>=1 && i<=(which==1?5u:1u)?0:UINT32_C(0xa5a5a5a5);
   actual=((uint32_t*)buffers[1].mapped)[i];
   if(actual!=expected){fprintf(stderr,"memexport GPU case%u word%u actual%08x expected%08x\n",which,i,actual,expected);goto done;}
  }
  g.vkDestroyPipeline(g.device,pipeline,NULL);pipeline=VK_NULL_HANDLE;g.vkDestroyShaderModule(g.device,module,NULL);module=VK_NULL_HANDLE;
 }
 printf("PASS: %u actual guest vertex memexport programs, exact stores and complete SSBO guards (%s) validation%u\n",(unsigned)XE_VERTEX_TEST_CASES,properties.deviceName,(unsigned)validation);result=0;
done:
 if(g.device){
  if(fence)g.vkDestroyFence(g.device,fence,NULL);
  if(commands)g.vkDestroyCommandPool(g.device,commands,NULL);
  if(pipeline)g.vkDestroyPipeline(g.device,pipeline,NULL);
  if(module)g.vkDestroyShaderModule(g.device,module,NULL);
  if(pool)g.vkDestroyDescriptorPool(g.device,pool,NULL);
  if(framebuffer)g.vkDestroyFramebuffer(g.device,framebuffer,NULL);
  if(render)g.vkDestroyRenderPass(g.device,render,NULL);
  if(layout)g.vkDestroyPipelineLayout(g.device,layout,NULL);
  for(i=0;i<2;++i)if(layouts[i])g.vkDestroyDescriptorSetLayout(g.device,layouts[i],NULL);
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
