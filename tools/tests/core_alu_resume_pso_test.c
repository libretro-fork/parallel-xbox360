/* C89 compiler-cost fixture. Numerical execution is a separate prerequisite.
 * Retain the first pipeline/cache; never label driver-cache state as cold. */
#define _POSIX_C_SOURCE 200809L
#define XE_SHADER_COMPILE_BENCH
#define XE_VK_TEST_NO_BUFFERS
#include "vulkan_test_context.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"resume PSO line %u\n",(unsigned)__LINE__); goto done; } } while (0)
int main(int argc,char **argv)
{
#ifdef _WIN32
   HMODULE loader=LoadLibraryA("vulkan-1.dll");FARPROC entry;
#else
   void *loader=dlopen("libvulkan.so.1",RTLD_NOW|RTLD_LOCAL),*entry;
#endif
   VkPhysicalDevice physical[8];VkQueueFamilyProperties families[32];
   VkPhysicalDeviceProperties properties;
   VkApplicationInfo app={0};VkInstanceCreateInfo instance={0};
   VkDeviceQueueCreateInfo queue={0};VkDeviceCreateInfo device={0};
   VkDescriptorSetLayoutBinding bindings[6];VkDescriptorSetLayoutCreateInfo set_info={0};
   VkDescriptorSetLayout set=VK_NULL_HANDLE;VkPipelineLayoutCreateInfo layout_info={0};
   VkPipelineLayout layout=VK_NULL_HANDLE;VkShaderModule module=VK_NULL_HANDLE;
   VkPipelineCacheCreateInfo cache_info={0};VkPipelineCache cache=VK_NULL_HANDLE;
   VkComputePipelineCreateInfo pipeline_info={0};VkPipeline pipelines[2]={VK_NULL_HANDLE,VK_NULL_HANDLE};
   uint32_t count=8,family_count=32,family,i;float priority=1.0f;int result=1;
   CHECK(argc==2 && loader);
#ifdef _WIN32
   entry=GetProcAddress(loader,"vkGetInstanceProcAddr");
#else
   entry=dlsym(loader,"vkGetInstanceProcAddr");
#endif
   memcpy(&g.get,&entry,sizeof(entry));CHECK(g.get);
   LOAD(vkCreateInstance);
   app.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO;app.apiVersion=VK_API_VERSION_1_0;
   instance.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;instance.pApplicationInfo=&app;
   OK(g.vkCreateInstance(&instance,NULL,&g.instance));
   LOAD(vkDestroyInstance);LOAD(vkEnumeratePhysicalDevices);LOAD(vkGetPhysicalDeviceProperties);
   LOAD(vkGetPhysicalDeviceQueueFamilyProperties);LOAD(vkCreateDevice);
   OK(g.vkEnumeratePhysicalDevices(g.instance,&count,physical));CHECK(count && count<=8);
   g.vkGetPhysicalDeviceProperties(physical[0],&properties);
   CHECK(properties.limits.maxPerStageDescriptorStorageBuffers>=5 && properties.limits.maxDescriptorSetStorageBuffers>=5);
   printf("RESUME_PSO_DEVICE vendor=%u device=%u driver=%u name=%s\n",properties.vendorID,properties.deviceID,properties.driverVersion,properties.deviceName);
   g.vkGetPhysicalDeviceQueueFamilyProperties(physical[0],&family_count,families);CHECK(family_count && family_count<=32);
   for(family=0;family<family_count;++family)if(families[family].queueFlags&VK_QUEUE_COMPUTE_BIT)break;
   CHECK(family<family_count);
   queue.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;queue.queueFamilyIndex=family;queue.queueCount=1;queue.pQueuePriorities=&priority;
   device.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;device.queueCreateInfoCount=1;device.pQueueCreateInfos=&queue;
   OK(g.vkCreateDevice(physical[0],&device,NULL,&g.device));
   LOAD(vkDestroyDevice);LOAD(vkCreateDescriptorSetLayout);LOAD(vkDestroyDescriptorSetLayout);
   LOAD(vkCreatePipelineLayout);LOAD(vkDestroyPipelineLayout);LOAD(vkCreateShaderModule);LOAD(vkDestroyShaderModule);
   LOAD(vkCreateComputePipelines);LOAD(vkDestroyPipeline);LOAD(vkCreatePipelineCache);LOAD(vkDestroyPipelineCache);
   memset(bindings,0,sizeof(bindings));
   for(i=0;i<6;++i){bindings[i].binding=i;bindings[i].descriptorType=i==4?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;bindings[i].descriptorCount=1;bindings[i].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;}
   set_info.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;set_info.bindingCount=6;set_info.pBindings=bindings;
   OK(g.vkCreateDescriptorSetLayout(g.device,&set_info,NULL,&set));
   layout_info.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;layout_info.setLayoutCount=1;layout_info.pSetLayouts=&set;
   OK(g.vkCreatePipelineLayout(g.device,&layout_info,NULL,&layout));
   xe_test_module_stage="raw_resume.module";CHECK(shader(argv[1],&module));
   cache_info.sType=VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
   OK(g.vkCreatePipelineCache(g.device,&cache_info,NULL,&cache));
   pipeline_info.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;pipeline_info.layout=layout;
   pipeline_info.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
   pipeline_info.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;pipeline_info.stage.module=module;pipeline_info.stage.pName="main";
   for(i=0;i<2;++i){
      XE_COMPILE_BEGIN();
      OK(g.vkCreateComputePipelines(g.device,cache,1,&pipeline_info,NULL,&pipelines[i]));
      XE_COMPILE_END(i?"raw_resume.compute_pso_warm":"raw_resume.compute_pso",0);
   }
   printf("PASS: resume compiler-only pipelines=2 bindings=6; no execution or cold-cache claim\n");
   result=ferror(stdout)?1:0;
done:
   if(g.device){
      for(i=0;i<2;++i)if(pipelines[i] && g.vkDestroyPipeline)g.vkDestroyPipeline(g.device,pipelines[i],NULL);
      if(cache && g.vkDestroyPipelineCache)g.vkDestroyPipelineCache(g.device,cache,NULL);
      if(module && g.vkDestroyShaderModule)g.vkDestroyShaderModule(g.device,module,NULL);
      if(layout && g.vkDestroyPipelineLayout)g.vkDestroyPipelineLayout(g.device,layout,NULL);
      if(set && g.vkDestroyDescriptorSetLayout)g.vkDestroyDescriptorSetLayout(g.device,set,NULL);
      if(g.vkDestroyDevice)g.vkDestroyDevice(g.device,NULL);
   }
   if(g.instance && g.vkDestroyInstance)g.vkDestroyInstance(g.instance,NULL);
#ifdef _WIN32
   if(loader)FreeLibrary(loader);
#else
   if(loader)dlclose(loader);
#endif
   return result;
}
