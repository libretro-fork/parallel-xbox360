/* Exact instanced transfer vertices through Vulkan transform feedback. C89. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#define XE_VK_TEST_GRAPHICS
#define XE_VK_TEST_VERTEX_INPUT
#include "vulkan_test_context.h"
#include "shader_compile_bench.h"
#include "libretro/core_transfer_geometry.h"
enum {RECTANGLES=257, OUTPUT_BYTES=RECTANGLES*6*16};
int main(int argc,char **argv) {
#ifdef _WIN32
 HMODULE loader=LoadLibraryA("vulkan-1.dll");FARPROC entry;
#else
 void *loader=dlopen("libvulkan.so.1",RTLD_NOW|RTLD_LOCAL),*entry;
#endif
 VkApplicationInfo app={0};VkInstanceCreateInfo instance_info={0};
 VkPhysicalDevice physical[8];VkPhysicalDeviceProperties properties;
 VkPhysicalDeviceFeatures2 features={0};VkPhysicalDeviceTransformFeedbackFeaturesEXT tf={0};
 VkQueueFamilyProperties families[32];VkDeviceQueueCreateInfo queue_info={0};
 VkDeviceCreateInfo device_info={0};VkQueue queue=VK_NULL_HANDLE;
 const char *extension=VK_EXT_TRANSFORM_FEEDBACK_EXTENSION_NAME;
 VkPipelineLayoutCreateInfo layout_info={0};
 VkVertexInputBindingDescription binding={0};VkVertexInputAttributeDescription attribute={0};
 VkPipelineLayout layout=VK_NULL_HANDLE;VkShaderModule module=VK_NULL_HANDLE;VkPipeline pipeline=VK_NULL_HANDLE;
 VkPipelineShaderStageCreateInfo stage={0};VkPipelineVertexInputStateCreateInfo vertex={0};
 VkPipelineInputAssemblyStateCreateInfo assembly={0};VkPipelineViewportStateCreateInfo viewport_state={0};
 VkPipelineRasterizationStateCreateInfo raster={0};VkPipelineMultisampleStateCreateInfo multisample={0};
 VkGraphicsPipelineCreateInfo pipeline_info={0};VkViewport viewport={0};VkRect2D scissor={{0,0},{1,1}};
 VkSubpassDescription subpass={0};VkRenderPassCreateInfo render_info={0};VkRenderPass render=VK_NULL_HANDLE;
 VkFramebufferCreateInfo framebuffer_info={0};VkFramebuffer framebuffer=VK_NULL_HANDLE;
 VkCommandPoolCreateInfo command_info={0};VkCommandPool commands=VK_NULL_HANDLE;
 VkCommandBufferAllocateInfo allocate_command={0};VkCommandBuffer command;
 VkCommandBufferBeginInfo begin={0};VkRenderPassBeginInfo render_begin={0};
 VkFenceCreateInfo fence_info={0};VkFence fence=VK_NULL_HANDLE;VkSubmitInfo submit={0};
 VkMemoryBarrier barrier={0};struct buffer buffers[2];
 VkDeviceSize zero=0,feedback_size=OUTPUT_BYTES;
 PFN_vkCmdBindVertexBuffers bind_vertices;
 float priority=1.0f;uint32_t count,family,i,j;float rectangles[RECTANGLES][4],expected[4];
 static const unsigned corners[6][2]={{0,1},{0,3},{2,1},{2,1},{0,3},{2,3}};
 int result=1;
 (void)shader;
 memset(buffers,0,sizeof(buffers));
 if(argc!=2 || strlen(argv[1])>400 || !loader)goto done;
#ifdef _WIN32
 entry=GetProcAddress(loader,"vkGetInstanceProcAddr");
#else
 entry=dlsym(loader,"vkGetInstanceProcAddr");
#endif
 memcpy(&g.get,&entry,sizeof(g.get));if(!g.get)goto done;
 LOAD(vkCreateInstance);
 app.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO;app.apiVersion=VK_MAKE_API_VERSION(0,1,2,0);
 instance_info.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;instance_info.pApplicationInfo=&app;
 OK(g.vkCreateInstance(&instance_info,NULL,&g.instance));
 LOAD(vkDestroyInstance);LOAD(vkEnumeratePhysicalDevices);LOAD(vkGetPhysicalDeviceProperties);
 LOAD(vkGetPhysicalDeviceMemoryProperties);LOAD(vkGetPhysicalDeviceQueueFamilyProperties);
 LOAD(vkGetPhysicalDeviceFeatures2);LOAD(vkCreateDevice);
 count=8;OK(g.vkEnumeratePhysicalDevices(g.instance,&count,physical));if(!count)goto done;
 g.vkGetPhysicalDeviceProperties(physical[0],&properties);
 fprintf(stderr,"BENCH_DEVICE Vulkan vendor=%u device=%u driver=%u name=%s\n",properties.vendorID,properties.deviceID,properties.driverVersion,properties.deviceName);
 g.vkGetPhysicalDeviceMemoryProperties(physical[0],&g.memory);
 tf.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT;
 features.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;features.pNext=&tf;
 g.vkGetPhysicalDeviceFeatures2(physical[0],&features);
 if(!tf.transformFeedback){fprintf(stderr,"Transform feedback unsupported on %s\n",properties.deviceName);goto done;}
 count=32;g.vkGetPhysicalDeviceQueueFamilyProperties(physical[0],&count,families);
 for(family=0;family<count;++family)if(families[family].queueFlags&VK_QUEUE_GRAPHICS_BIT)break;
 if(family==count)goto done;
 queue_info.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;queue_info.queueFamilyIndex=family;
 queue_info.queueCount=1;queue_info.pQueuePriorities=&priority;
 tf.geometryStreams=VK_FALSE;
 device_info.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;device_info.pNext=&tf;
 device_info.queueCreateInfoCount=1;device_info.pQueueCreateInfos=&queue_info;
 device_info.enabledExtensionCount=1;device_info.ppEnabledExtensionNames=&extension;
 OK(g.vkCreateDevice(physical[0],&device_info,NULL,&g.device));
 LOAD(vkDestroyDevice);LOAD(vkGetDeviceQueue);LOAD(vkCmdBindVertexBuffers);bind_vertices=g.vkCmdBindVertexBuffers;LOAD(vkCreateBuffer);LOAD(vkDestroyBuffer);
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
 LOAD(vkCmdBindTransformFeedbackBuffersEXT);LOAD(vkCmdBeginTransformFeedbackEXT);LOAD(vkCmdEndTransformFeedbackEXT);
 LOAD(vkCreateFence);LOAD(vkDestroyFence);LOAD(vkResetFences);LOAD(vkQueueSubmit);LOAD(vkWaitForFences);
 g.vkGetDeviceQueue(g.device,family,0,&queue);
 for(i=0;i<2;++i)if(!make_buffer(&buffers[i],OUTPUT_BYTES))goto done;
 for(i=0;i<RECTANGLES;++i) {
  rectangles[i][0]=-1.0f+(float)i/512.0f;rectangles[i][1]=1.0f-(float)i/512.0f;
  rectangles[i][2]=rectangles[i][0]+.125f;rectangles[i][3]=rectangles[i][1]-.25f;
 }
 memcpy(buffers[0].mapped,rectangles,sizeof(rectangles));
 if(!cache_buffer(&buffers[0],0))goto done;
 layout_info.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
 OK(g.vkCreatePipelineLayout(g.device,&layout_info,NULL,&layout));
 subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;
 render_info.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;render_info.subpassCount=1;render_info.pSubpasses=&subpass;
 OK(g.vkCreateRenderPass(g.device,&render_info,NULL,&render));
 framebuffer_info.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;framebuffer_info.renderPass=render;
 framebuffer_info.width=framebuffer_info.height=framebuffer_info.layers=1;
 OK(g.vkCreateFramebuffer(g.device,&framebuffer_info,NULL,&framebuffer));
 command_info.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;command_info.queueFamilyIndex=family;
 OK(g.vkCreateCommandPool(g.device,&command_info,NULL,&commands));
 allocate_command.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;allocate_command.commandPool=commands;
 allocate_command.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;allocate_command.commandBufferCount=1;
 OK(g.vkAllocateCommandBuffers(g.device,&allocate_command,&command));
 fence_info.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;OK(g.vkCreateFence(g.device,&fence_info,NULL,&fence));
 stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;stage.stage=VK_SHADER_STAGE_VERTEX_BIT;stage.pName="main";
 vertex.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
 binding.stride=16;binding.inputRate=VK_VERTEX_INPUT_RATE_INSTANCE;
 attribute.format=VK_FORMAT_R32G32B32A32_SFLOAT;
 vertex.vertexBindingDescriptionCount=vertex.vertexAttributeDescriptionCount=1;
 vertex.pVertexBindingDescriptions=&binding;vertex.pVertexAttributeDescriptions=&attribute;
 assembly.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
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
 if(!shader(argv[1],&module))goto done;
 stage.module=module;
 OK(g.vkCreateGraphicsPipelines(g.device,VK_NULL_HANDLE,1,&pipeline_info,NULL,&pipeline));
 memset(buffers[1].mapped,0xA5,OUTPUT_BYTES);if(!cache_buffer(&buffers[1],0))goto done;
  OK(g.vkResetCommandPool(g.device,commands,0));OK(g.vkBeginCommandBuffer(command,&begin));
  barrier.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT|VK_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT;
  g.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_VERTEX_INPUT_BIT|VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT,0,1,&barrier,0,NULL,0,NULL);
  g.vkCmdBeginRenderPass(command,&render_begin,VK_SUBPASS_CONTENTS_INLINE);
  g.vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);
  bind_vertices(command,0,1,&buffers[0].buffer,&zero);
  g.vkCmdBindTransformFeedbackBuffersEXT(command,0,1,&buffers[1].buffer,&zero,&feedback_size);
  g.vkCmdBeginTransformFeedbackEXT(command,0,0,NULL,NULL);g.vkCmdDraw(command,6,RECTANGLES,0,0);
  g.vkCmdEndTransformFeedbackEXT(command,0,0,NULL,NULL);g.vkCmdEndRenderPass(command);
  barrier.srcAccessMask=VK_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
  g.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,NULL,0,NULL);
  OK(g.vkEndCommandBuffer(command));OK(g.vkResetFences(g.device,1,&fence));OK(g.vkQueueSubmit(queue,1,&submit,fence));
  if(g.vkWaitForFences(g.device,1,&fence,VK_TRUE,UINT64_C(10000000000))!=VK_SUCCESS){fprintf(stderr,"GPU completion failed, retaining pending resources\n");exit(1);}
  if(!cache_buffer(&buffers[1],1))goto done;
  for(i=0;i<RECTANGLES;++i)for(j=0;j<6;++j){
   expected[0]=rectangles[i][corners[j][0]];expected[1]=rectangles[i][corners[j][1]];
   expected[2]=0;expected[3]=1;
   if(memcmp((float*)buffers[1].mapped+(i*6+j)*4,expected,16)){
    fprintf(stderr,"Transfer mismatch instance %u vertex %u\n",i,j);goto done;
   }
  }
  puts("PASS: Vulkan 257 rectangles, 1542 exact ordered transfer vertices");result=0;

done:
 if(g.device){
  if(fence)g.vkDestroyFence(g.device,fence,NULL);
  if(commands)g.vkDestroyCommandPool(g.device,commands,NULL);
  if(pipeline)g.vkDestroyPipeline(g.device,pipeline,NULL);
  if(module)g.vkDestroyShaderModule(g.device,module,NULL);

  if(framebuffer)g.vkDestroyFramebuffer(g.device,framebuffer,NULL);
  if(render)g.vkDestroyRenderPass(g.device,render,NULL);
  if(layout)g.vkDestroyPipelineLayout(g.device,layout,NULL);

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
