/* Driver PSO timing of real procedural guest VS+PS translations. C89.
 * Does not execute draws; numerical execution remains a separate required test. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#define XE_VK_TEST_GRAPHICS
#define XE_VK_TEST_NO_BUFFERS
#define XE_VK_TEST_SHADER_MAX_BYTES 8388608
#define XE_SHADER_COMPILE_BENCH
#include "vulkan_test_context.h"
int main(int argc,char **argv)
{
#ifdef _WIN32
   HMODULE loader=LoadLibraryA("vulkan-1.dll");FARPROC entry;
#else
   void *loader=dlopen("libvulkan.so.1",RTLD_NOW|RTLD_LOCAL),*entry;
#endif
   const char *rtz=getenv("XE_SHADER_BENCH_RTZ_PROFILE");
   unsigned hardware_rtz=rtz && !strcmp(rtz,"1");

   const char *pso_stage=hardware_rtz?"vulkan.rtz_vs_ps_pso":"vulkan.vs_ps_pso";
   VkPhysicalDeviceProperties2 properties2={0};VkPhysicalDeviceFloatControlsProperties controls={0};
   PFN_vkGetPhysicalDeviceProperties2 get_properties2;PFN_vkVoidFunction raw_properties2;
   VkApplicationInfo app={0};VkInstanceCreateInfo instance={0};
   VkPhysicalDevice physical[8];VkPhysicalDeviceProperties properties;
   VkQueueFamilyProperties families[32];VkDeviceQueueCreateInfo queue={0};
   VkDeviceCreateInfo device={0};VkDescriptorSetLayoutBinding bindings[5];
   VkDescriptorSetLayoutCreateInfo set={0};VkDescriptorSetLayout layouts[2]={0};
   VkPipelineLayoutCreateInfo layout_info={0};VkPipelineLayout layout=VK_NULL_HANDLE;
   VkAttachmentDescription attachment={0};VkAttachmentReference color={0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
   VkSubpassDescription subpass={0};VkRenderPassCreateInfo render_info={0};VkRenderPass render=VK_NULL_HANDLE;
   VkPipelineShaderStageCreateInfo stages[2];VkShaderModule modules[2]={0};
   VkPipelineVertexInputStateCreateInfo vertex={0};VkPipelineInputAssemblyStateCreateInfo assembly={0};
   VkPipelineViewportStateCreateInfo view={0};VkViewport viewport={0};VkRect2D scissor={{0,0},{1,1}};
   VkPipelineRasterizationStateCreateInfo raster={0};VkPipelineMultisampleStateCreateInfo multisample={0};
   VkPipelineColorBlendAttachmentState blend={0};VkPipelineColorBlendStateCreateInfo blends={0};
   VkGraphicsPipelineCreateInfo info={0};VkPipeline pipeline=VK_NULL_HANDLE;
   char path[512];uint32_t count,family,i,which,variant;float priority=1;
   int result=1;
   if(argc!=2 || strlen(argv[1])>400 || !loader)goto done;
#ifdef _WIN32
   entry=GetProcAddress(loader,"vkGetInstanceProcAddr");
#else
   entry=dlsym(loader,"vkGetInstanceProcAddr");
#endif
   memcpy(&g.get,&entry,sizeof(g.get));if(!g.get)goto done;
   LOAD(vkCreateInstance);app.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO;app.apiVersion=hardware_rtz?VK_API_VERSION_1_2:VK_API_VERSION_1_1;
   instance.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;instance.pApplicationInfo=&app;
   OK(g.vkCreateInstance(&instance,NULL,&g.instance));
   LOAD(vkDestroyInstance);LOAD(vkEnumeratePhysicalDevices);LOAD(vkGetPhysicalDeviceProperties);
   LOAD(vkGetPhysicalDeviceQueueFamilyProperties);LOAD(vkCreateDevice);
   count=8;OK(g.vkEnumeratePhysicalDevices(g.instance,&count,physical));if(!count)goto done;
   g.vkGetPhysicalDeviceProperties(physical[0],&properties);
   fprintf(stderr,"BENCH_DEVICE Vulkan vendor=%u device=%u driver=%u name=%s\n",
         properties.vendorID,properties.deviceID,properties.driverVersion,properties.deviceName);
   if(hardware_rtz){
      raw_properties2=g.get(g.instance,"vkGetPhysicalDeviceProperties2");if(!raw_properties2)goto done;
      memcpy(&get_properties2,&raw_properties2,sizeof(get_properties2));
      properties2.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
      controls.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FLOAT_CONTROLS_PROPERTIES;properties2.pNext=&controls;
      get_properties2(physical[0],&properties2);
      if(!controls.shaderRoundingModeRTZFloat32 || !controls.shaderSignedZeroInfNanPreserveFloat32){
         fputs("Required hardware RTZ/signed-zero profile unsupported\n",stderr);goto done;
      }
   }
   count=32;g.vkGetPhysicalDeviceQueueFamilyProperties(physical[0],&count,families);
   for(family=0;family<count;++family)if(families[family].queueFlags&VK_QUEUE_GRAPHICS_BIT)break;
   if(family==count)goto done;
   queue.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;queue.queueFamilyIndex=family;queue.queueCount=1;queue.pQueuePriorities=&priority;
   device.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;device.queueCreateInfoCount=1;device.pQueueCreateInfos=&queue;
   OK(g.vkCreateDevice(physical[0],&device,NULL,&g.device));
   LOAD(vkDestroyDevice);LOAD(vkCreateDescriptorSetLayout);LOAD(vkDestroyDescriptorSetLayout);
   LOAD(vkCreatePipelineLayout);LOAD(vkDestroyPipelineLayout);LOAD(vkCreateShaderModule);LOAD(vkDestroyShaderModule);
   LOAD(vkCreateRenderPass);LOAD(vkDestroyRenderPass);LOAD(vkCreateGraphicsPipelines);LOAD(vkDestroyPipeline);
   xe_test_module_stage=hardware_rtz?"vulkan.rtz_module":"vulkan.module";
   memset(bindings,0,sizeof(bindings));
   for(i=0;i<2;++i){bindings[i].binding=i;bindings[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      bindings[i].descriptorCount=i?1:4;bindings[i].stageFlags=VK_SHADER_STAGE_ALL_GRAPHICS;}
   set.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;set.bindingCount=2;set.pBindings=bindings;
   OK(g.vkCreateDescriptorSetLayout(g.device,&set,NULL,&layouts[0]));
   for(i=0;i<5;++i){bindings[i].binding=i;bindings[i].descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      bindings[i].descriptorCount=1;bindings[i].stageFlags=VK_SHADER_STAGE_ALL_GRAPHICS;}
   set.bindingCount=5;OK(g.vkCreateDescriptorSetLayout(g.device,&set,NULL,&layouts[1]));
   layout_info.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;layout_info.setLayoutCount=2;layout_info.pSetLayouts=layouts;
   OK(g.vkCreatePipelineLayout(g.device,&layout_info,NULL,&layout));
   attachment.format=VK_FORMAT_R8G8B8A8_UNORM;attachment.samples=VK_SAMPLE_COUNT_1_BIT;
   attachment.loadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;attachment.storeOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
   attachment.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;attachment.finalLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
   subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;subpass.colorAttachmentCount=1;subpass.pColorAttachments=&color;
   render_info.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;render_info.attachmentCount=1;render_info.pAttachments=&attachment;
   render_info.subpassCount=1;render_info.pSubpasses=&subpass;OK(g.vkCreateRenderPass(g.device,&render_info,NULL,&render));
   memset(stages,0,sizeof(stages));for(i=0;i<2;++i){stages[i].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
      stages[i].stage=i?VK_SHADER_STAGE_FRAGMENT_BIT:VK_SHADER_STAGE_VERTEX_BIT;stages[i].pName="main";}
   vertex.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
   assembly.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
   viewport.width=viewport.height=1;viewport.maxDepth=1;view.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
   view.viewportCount=view.scissorCount=1;view.pViewports=&viewport;view.pScissors=&scissor;
   raster.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;raster.lineWidth=1;
   multisample.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;multisample.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
   blend.colorWriteMask=15;blends.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;blends.attachmentCount=1;blends.pAttachments=&blend;
   info.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;info.stageCount=2;info.pStages=stages;
   info.pVertexInputState=&vertex;info.pInputAssemblyState=&assembly;info.pViewportState=&view;
   info.pRasterizationState=&raster;info.pMultisampleState=&multisample;info.pColorBlendState=&blends;info.layout=layout;info.renderPass=render;
   for(which=0;which<8;++which) {
      for(i=0;i<2;++i){sprintf(path,"%s/shader%03u.spv",argv[1],which+i*8);
         XE_COMPILE_CASE(which*2+i);if(!shader(path,&modules[i]))goto done;stages[i].module=modules[i];}
      for(variant=0;variant<2;++variant){
         assembly.topology=variant?VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
         blend.blendEnable=variant?VK_TRUE:VK_FALSE;
         blend.srcColorBlendFactor=VK_BLEND_FACTOR_ONE;blend.dstColorBlendFactor=VK_BLEND_FACTOR_ZERO;
         blend.srcAlphaBlendFactor=VK_BLEND_FACTOR_ONE;blend.dstAlphaBlendFactor=VK_BLEND_FACTOR_ZERO;
         XE_COMPILE_BEGIN();OK(g.vkCreateGraphicsPipelines(g.device,VK_NULL_HANDLE,1,&info,NULL,&pipeline));
         XE_COMPILE_END(pso_stage,which*2+variant);
         g.vkDestroyPipeline(g.device,pipeline,NULL);pipeline=VK_NULL_HANDLE;
      }
      for(i=0;i<2;++i){g.vkDestroyShaderModule(g.device,modules[i],NULL);modules[i]=VK_NULL_HANDLE;}
   }
   puts("PASS:16 VS+PS PSOs, control-flow/ALU stress, two topology/blend recipes, null explicit pipeline cache");result=0;
done:
   if(g.device){if(pipeline)g.vkDestroyPipeline(g.device,pipeline,NULL);
      for(i=0;i<2;++i)if(modules[i])g.vkDestroyShaderModule(g.device,modules[i],NULL);
      if(render)g.vkDestroyRenderPass(g.device,render,NULL);
      if(layout)g.vkDestroyPipelineLayout(g.device,layout,NULL);
      for(i=0;i<2;++i)if(layouts[i])g.vkDestroyDescriptorSetLayout(g.device,layouts[i],NULL);
      g.vkDestroyDevice(g.device,NULL);}
   if(g.instance && g.vkDestroyInstance)g.vkDestroyInstance(g.instance,NULL);
#ifdef _WIN32
   if(loader)FreeLibrary(loader);
#else
   if(loader)dlclose(loader);
#endif
   return result;
}
