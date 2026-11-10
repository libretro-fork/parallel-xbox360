/* Real Vulkan acceptance of borrowed descriptor layout input. C89; no window. */
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#define VK_NO_PROTOTYPES
#include "libretro/core_vk_layout_input.h"
#define FN(name) PFN_##name name=NULL
#define LOAD(name) do{PFN_vkVoidFunction p=get(instance,#name);memcpy(&name,&p,sizeof(name));if(!name)goto finish;}while(0)
#define OK(call) do{if((call)!=VK_SUCCESS)goto finish;}while(0)
int main(void){
#ifdef _WIN32
 HMODULE loader=LoadLibraryA("vulkan-1.dll");FARPROC symbol;
#else
 void* loader=dlopen("libvulkan.so.1",RTLD_NOW|RTLD_LOCAL);void* symbol;
#endif
 PFN_vkGetInstanceProcAddr get=NULL;FN(vkCreateInstance);FN(vkDestroyInstance);
 FN(vkEnumeratePhysicalDevices);FN(vkGetPhysicalDeviceQueueFamilyProperties);FN(vkCreateDevice);FN(vkDestroyDevice);
 FN(vkCreateDescriptorSetLayout);FN(vkDestroyDescriptorSetLayout);FN(vkCreatePipelineLayout);FN(vkDestroyPipelineLayout);
 VkInstance instance=VK_NULL_HANDLE;VkDevice device=VK_NULL_HANDLE;VkPhysicalDevice physical[8];
 VkQueueFamilyProperties queues[32];VkApplicationInfo app={0};VkInstanceCreateInfo info={0};
 VkDeviceQueueCreateInfo queue={0};VkDeviceCreateInfo create={0};
 VkDescriptorSetLayout layouts[2]={VK_NULL_HANDLE,VK_NULL_HANDLE};VkPipelineLayout pipeline=VK_NULL_HANDLE;
 VkDescriptorSetLayoutCreateInfo descriptor={0};VkPipelineLayoutCreateInfo pipeline_info={0};
 xe_descriptor_scratch scratch={0};VkDescriptorSetLayoutBinding* bindings;
 uint32_t count,family,i,stage;float priority=1.0f;int result=1;
 if(!loader)return 1;
#ifdef _WIN32
 symbol=GetProcAddress(loader,"vkGetInstanceProcAddr");
#else
 symbol=dlsym(loader,"vkGetInstanceProcAddr");
#endif
 memcpy(&get,&symbol,sizeof(get));if(!get)goto finish;
 LOAD(vkCreateInstance);app.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO;app.apiVersion=VK_API_VERSION_1_1;
 info.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;info.pApplicationInfo=&app;
 OK(vkCreateInstance(&info,NULL,&instance));LOAD(vkDestroyInstance);LOAD(vkEnumeratePhysicalDevices);
 LOAD(vkGetPhysicalDeviceQueueFamilyProperties);LOAD(vkCreateDevice);count=8;
 OK(vkEnumeratePhysicalDevices(instance,&count,physical));if(!count)goto finish;
 count=32;vkGetPhysicalDeviceQueueFamilyProperties(physical[0],&count,queues);
 for(family=0;family<count;++family)if(queues[family].queueCount && (queues[family].queueFlags&VK_QUEUE_GRAPHICS_BIT))break;
 if(family==count)goto finish;
 queue.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;queue.queueFamilyIndex=family;queue.queueCount=1;queue.pQueuePriorities=&priority;
 create.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;create.queueCreateInfoCount=1;create.pQueueCreateInfos=&queue;
 OK(vkCreateDevice(physical[0],&create,NULL,&device));LOAD(vkDestroyDevice);LOAD(vkCreateDescriptorSetLayout);
 LOAD(vkDestroyDescriptorSetLayout);LOAD(vkCreatePipelineLayout);LOAD(vkDestroyPipelineLayout);
 for(i=0;i<16;++i){
  for(stage=0;stage<2;++stage){bindings=xe_vk_layout_bindings(&scratch,1+i,1+i/2,stage?VK_SHADER_STAGE_FRAGMENT_BIT:VK_SHADER_STAGE_VERTEX_BIT);if(!bindings)goto finish;
   descriptor.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;descriptor.bindingCount=2+i+i/2;descriptor.pBindings=bindings;
   OK(vkCreateDescriptorSetLayout(device,&descriptor,NULL,&layouts[stage]));}
  pipeline_info.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;pipeline_info.setLayoutCount=2;pipeline_info.pSetLayouts=layouts;
  OK(vkCreatePipelineLayout(device,&pipeline_info,NULL,&pipeline));
  vkDestroyPipelineLayout(device,pipeline,NULL);pipeline=VK_NULL_HANDLE;
  for(stage=0;stage<2;++stage){vkDestroyDescriptorSetLayout(device,layouts[stage],NULL);layouts[stage]=VK_NULL_HANDLE;}
 }
 result=0;puts("PASS: 32 actual Vulkan descriptor layouts and 16 pipeline layouts from borrowed C89 input; correct destruction order");
finish:
 if(pipeline && vkDestroyPipelineLayout)vkDestroyPipelineLayout(device,pipeline,NULL);
 for(stage=0;stage<2;++stage)if(layouts[stage] && vkDestroyDescriptorSetLayout)vkDestroyDescriptorSetLayout(device,layouts[stage],NULL);
 xe_descriptor_scratch_free(&scratch);
 if(device && vkDestroyDevice)vkDestroyDevice(device,NULL);
 if(instance && vkDestroyInstance)vkDestroyInstance(instance,NULL);
#ifdef _WIN32
 FreeLibrary(loader);
#else
 dlclose(loader);
#endif
 return result;
}
