/* Complete single-bank Vulkan constructor accounting. Test-only; BSD; C89.
 * Shared device/vertex/layout/render-pass setup is outside constructor timing.
 * First transfer checked separately; no internal driver allocation-byte or
 * cold-driver-cache acceptance. */
#define XE_VK_TEST_GPU_TIMING
#define main identity_reference_unused_main
#include "transfer_identity_msaa_reference_vulkan_test.c"
#undef main
#ifndef _WIN32
#include <time.h>
#endif
static struct {
 unsigned modules,module_live,module_peak,layouts,layout_live,calls,pipelines,pipeline_live,failed;
 size_t emitted_bytes,max_module_bytes;
 double module_seconds,layout_seconds,pipeline_seconds;
} init_profile;
static double init_now(void) {
#ifdef _WIN32
 LARGE_INTEGER tick,frequency;
 if(!QueryPerformanceCounter(&tick) || !QueryPerformanceFrequency(&frequency))exit(2);
 return (double)tick.QuadPart/(double)frequency.QuadPart;
#else
 struct timespec tick;if(clock_gettime(CLOCK_MONOTONIC,&tick))exit(2);
 return (double)tick.tv_sec+(double)tick.tv_nsec*1e-9;
#endif
}
static VKAPI_ATTR VkResult VKAPI_CALL init_shader(VkDevice device,const VkShaderModuleCreateInfo *info,const VkAllocationCallbacks *allocator,VkShaderModule *out) {
 double before=init_now();VkResult result=g.vkCreateShaderModule(device,info,allocator,out);
 init_profile.module_seconds+=init_now()-before;++init_profile.modules;
 init_profile.emitted_bytes+=info->codeSize;if(info->codeSize>init_profile.max_module_bytes)init_profile.max_module_bytes=info->codeSize;
 if(*out){++init_profile.module_live;if(init_profile.module_live>init_profile.module_peak)init_profile.module_peak=init_profile.module_live;}
 return result;
}
static VKAPI_ATTR void VKAPI_CALL init_destroy_shader(VkDevice device,VkShaderModule shader,const VkAllocationCallbacks *allocator) {
 if(shader){if(!init_profile.module_live)++init_profile.failed;else --init_profile.module_live;}g.vkDestroyShaderModule(device,shader,allocator);
}
static VKAPI_ATTR VkResult VKAPI_CALL init_layout(VkDevice device,const VkPipelineLayoutCreateInfo *info,const VkAllocationCallbacks *allocator,VkPipelineLayout *out) {
 double before=init_now();VkResult result=g.vkCreatePipelineLayout(device,info,allocator,out);
 init_profile.layout_seconds+=init_now()-before;++init_profile.layouts;if(*out)++init_profile.layout_live;return result;
}
static VKAPI_ATTR void VKAPI_CALL init_destroy_layout(VkDevice device,VkPipelineLayout layout,const VkAllocationCallbacks *allocator) {
 if(layout){if(!init_profile.layout_live)++init_profile.failed;else --init_profile.layout_live;}g.vkDestroyPipelineLayout(device,layout,allocator);
}
static VKAPI_ATTR VkResult VKAPI_CALL init_pipelines(VkDevice device,VkPipelineCache cache,uint32_t count,const VkGraphicsPipelineCreateInfo *info,const VkAllocationCallbacks *allocator,VkPipeline *out) {
 double before=init_now();VkResult result=g.vkCreateGraphicsPipelines(device,cache,count,info,allocator,out);uint32_t i;
 init_profile.pipeline_seconds+=init_now()-before;++init_profile.calls;init_profile.pipelines+=count;
 for(i=0;i<count;++i)if(out[i])++init_profile.pipeline_live;
 return result;
}
static VKAPI_ATTR void VKAPI_CALL init_destroy_pipeline(VkDevice device,VkPipeline pipeline,const VkAllocationCallbacks *allocator) {
 if(pipeline){if(!init_profile.pipeline_live)++init_profile.failed;else --init_profile.pipeline_live;}g.vkDestroyPipeline(device,pipeline,allocator);
}
static int init_unsigned(const char *text,unsigned limit,unsigned *out) {
 unsigned n=0;if(!*text)return 0;
 while(*text){if(*text<'0' || *text>'9' || n>limit/10)return 0;n=n*10+(unsigned)(*text++-'0');if(n>limit)return 0;}*out=n;return 1;
}
#include "transfer_identity_first_draw_vulkan.h"
int main(int argc,char **argv) {
#ifdef _WIN32
 HMODULE loader=LoadLibraryA("vulkan-1.dll");FARPROC entry;
#else
 void *loader=dlopen("libvulkan.so.1",RTLD_NOW|RTLD_LOCAL),*entry;
#endif
 xe_transfer_identity_vk_bank ready={0};xe_transfer_identity_vk_config config={0};xe_transfer_identity_vk_ops ops={0};xe_spv_binary scratch={0};
 VkApplicationInfo app={0};VkInstanceCreateInfo instance={0};VkPhysicalDevice physical[8];VkPhysicalDeviceProperties properties;
 VkPhysicalDeviceFeatures features={0},enabled={0};VkImageFormatProperties image_properties;VkSampleCountFlags samples[4]={0};
 VkPhysicalDeviceDynamicRenderingFeatures rendering={0};VkPhysicalDeviceFeatures2 features2={0};
 VkQueueFamilyProperties families[32];VkDeviceQueueCreateInfo queue={0};VkDeviceCreateInfo device={0};float priority=1;
 VkDescriptorSetLayoutBinding binding={0};VkDescriptorSetLayoutCreateInfo layout={0};
 VkShaderModule vertex=VK_NULL_HANDLE;VkDescriptorSetLayout images=VK_NULL_HANDLE;
 VkAttachmentDescription attachment={0};VkAttachmentReference reference={0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};VkSubpassDescription subpass={0};VkRenderPassCreateInfo pass={0};
 uint32_t count,family,intersection=UINT32_MAX;unsigned gamma,shading,dynamic,f,d,s,a,b,sample_index,scale_classes,expected=0,expected_modules=0,lookups=0,needed;
 const VkPipeline *found;double before,elapsed,query_seconds,setup_seconds;char path[4096];int result=1;VkResult status;
 identity_first_draw first={0};
 if(!loader || argc!=7 || strlen(argv[1])>2048 || !init_unsigned(argv[2],16,&config.scale_x) || !init_unsigned(argv[3],16,&config.scale_y) ||
  !config.scale_x || !config.scale_y || !init_unsigned(argv[4],1,&gamma) || !init_unsigned(argv[5],1,&shading) || !init_unsigned(argv[6],1,&dynamic))goto done;
#ifdef _WIN32
 entry=GetProcAddress(loader,"vkGetInstanceProcAddr");
#else
 entry=dlsym(loader,"vkGetInstanceProcAddr");
#endif
 memcpy(&g.get,&entry,sizeof(g.get));CHECK(g.get);LOAD(vkCreateInstance);
 app.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO;app.apiVersion=dynamic?VK_API_VERSION_1_3:VK_API_VERSION_1_0;
 instance.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;instance.pApplicationInfo=&app;OK(g.vkCreateInstance(&instance,NULL,&g.instance));
 LOAD(vkDestroyInstance);LOAD(vkEnumeratePhysicalDevices);LOAD(vkGetPhysicalDeviceProperties);LOAD(vkGetPhysicalDeviceQueueFamilyProperties);
 LOAD(vkCreateDevice);LOAD(vkDestroyDevice);IMAGE_FN(features,vkGetPhysicalDeviceFeatures);IMAGE_FN(properties,vkGetPhysicalDeviceImageFormatProperties);
 count=8;OK(g.vkEnumeratePhysicalDevices(g.instance,&count,physical));CHECK(count);g.vkGetPhysicalDeviceProperties(physical[0],&properties);im.features(physical[0],&features);
 CHECK(!shading || features.sampleRateShading);CHECK(features.pipelineStatisticsQuery);
 enabled.sampleRateShading=shading;enabled.pipelineStatisticsQuery=VK_TRUE;
 if(dynamic){CHECK(properties.apiVersion>=VK_API_VERSION_1_3);LOAD(vkGetPhysicalDeviceFeatures2);
  rendering.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;features2.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;features2.pNext=&rendering;
  g.vkGetPhysicalDeviceFeatures2(physical[0],&features2);CHECK(rendering.dynamicRendering);device.pNext=&rendering;}
 before=init_now();
 for(f=0;f<4;++f){if(f==1 && !gamma)continue;
  status=im.properties(physical[0],identity_ms_numeric_format(f),VK_IMAGE_TYPE_2D,VK_IMAGE_TILING_OPTIMAL,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,0,&image_properties);
  if(status==VK_ERROR_FORMAT_NOT_SUPPORTED)continue;
  CHECK(status==VK_SUCCESS);samples[f]=image_properties.sampleCounts;intersection&=samples[f];}
 CHECK(samples[0]&VK_SAMPLE_COUNT_1_BIT);config.native2=(intersection&VK_SAMPLE_COUNT_2_BIT)!=0;
 for(f=0;f<4;++f)config.sample_mask[f]=xe_transfer_identity_samples((unsigned)samples[f],config.native2);
 query_seconds=init_now()-before;
 count=32;g.vkGetPhysicalDeviceQueueFamilyProperties(physical[0],&count,families);for(family=0;family<count;++family)if(families[family].queueFlags&VK_QUEUE_GRAPHICS_BIT)break;CHECK(family<count);
 CHECK(families[family].timestampValidBits>=36);LOAD(vkGetPhysicalDeviceMemoryProperties);g.vkGetPhysicalDeviceMemoryProperties(physical[0],&g.memory);
 queue.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;queue.queueFamilyIndex=family;queue.queueCount=1;queue.pQueuePriorities=&priority;
 device.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;device.queueCreateInfoCount=1;device.pQueueCreateInfos=&queue;device.pEnabledFeatures=&enabled;OK(g.vkCreateDevice(physical[0],&device,NULL,&g.device));
 LOAD(vkCreateShaderModule);LOAD(vkDestroyShaderModule);LOAD(vkCreateDescriptorSetLayout);LOAD(vkDestroyDescriptorSetLayout);
 LOAD(vkCreatePipelineLayout);LOAD(vkDestroyPipelineLayout);LOAD(vkCreateGraphicsPipelines);LOAD(vkDestroyPipeline);LOAD(vkCreateRenderPass);LOAD(vkDestroyRenderPass);
 before=init_now();sprintf(path,"%s-vertex.spv",argv[1]);CHECK(shader(path,&vertex));
 binding.descriptorType=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;binding.descriptorCount=1;binding.stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT;
 layout.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;layout.bindingCount=1;layout.pBindings=&binding;OK(g.vkCreateDescriptorSetLayout(g.device,&layout,NULL,&images));
 attachment.loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;attachment.initialLayout=attachment.finalLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
 subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;subpass.colorAttachmentCount=1;subpass.pColorAttachments=&reference;
 pass.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;pass.attachmentCount=1;pass.pAttachments=&attachment;pass.subpassCount=1;pass.pSubpasses=&subpass;
 for(f=0;f<4;++f)for(d=0;d<3;++d)if(config.sample_mask[f]&(1u<<d)){
  attachment.format=identity_ms_numeric_format(f);attachment.samples=(VkSampleCountFlagBits)(d==1 && !config.native2?4:1u<<d);
  OK(g.vkCreateRenderPass(g.device,&pass,NULL,&config.passes[f][d]));}
 setup_seconds=init_now()-before;
 config.device=g.device;config.vertex=vertex;config.image_layout=images;config.sample_shading=shading;config.dynamic_rendering=dynamic;
 ops.create_shader=init_shader;ops.destroy_shader=init_destroy_shader;ops.create_layout=init_layout;ops.destroy_layout=init_destroy_layout;
 ops.create_pipelines=init_pipelines;ops.destroy_pipeline=init_destroy_pipeline;
 before=init_now();CHECK(xe_transfer_identity_vk_init(&ready,&config,&ops,&scratch));elapsed=init_now()-before;
 scale_classes=config.scale_x==1 && config.scale_y==1?1:4;
 for(d=0;d<3;++d)for(s=0;s<3;++s){needed=0;for(f=0;f<4;++f)if((config.sample_mask[f]&(1u<<d)) && (config.sample_mask[f]&(1u<<s))){expected+=scale_classes*(shading?1u:1u<<d);needed=1;}expected_modules+=needed*scale_classes;}
 CHECK(expected && init_profile.pipelines==expected && init_profile.pipeline_live==expected && init_profile.modules==expected_modules && !init_profile.module_live && init_profile.layouts==1 && init_profile.layout_live==1 && !init_profile.failed);
 for(f=0;f<4;++f)for(d=0;d<3;++d)for(s=0;s<3;++s)for(a=0;a<2;++a)for(b=0;b<2;++b){
  found=xe_transfer_identity_vk_get(&ready,f,d,s,a,b);needed=(config.sample_mask[f]&(1u<<d)) && (config.sample_mask[f]&(1u<<s));CHECK((found!=NULL)==(needed!=0));
  if(found)for(sample_index=0;sample_index<(shading?1u:1u<<d);++sample_index)CHECK(found[sample_index]!=VK_NULL_HANDLE);
  ++lookups;}
 CHECK(init_profile.pipelines==expected && init_profile.modules==expected_modules);
 CHECK(identity_first_transfer(&ready,&config,family,families[family].timestampValidBits,properties.limits.timestampPeriod,&first));
 CHECK(init_profile.pipelines==expected && init_profile.modules==expected_modules && init_profile.layouts==1);
 CHECK(xe_transfer_identity_vk_close(&ready,&ops));CHECK(!init_profile.pipeline_live && !init_profile.layout_live && !init_profile.module_live && !init_profile.failed);
 printf("INIT_VULKAN device%s driver%u scale%u,%u gamma%u native%u shading%u dynamic%u masks%u,%u,%u,%u\n",properties.deviceName,properties.driverVersion,config.scale_x,config.scale_y,gamma,config.native2,shading,dynamic,config.sample_mask[0],config.sample_mask[1],config.sample_mask[2],config.sample_mask[3]);
 printf("INIT_COST query_seconds=%.17g shared_setup_seconds=%.17g bank_seconds=%.17g module_seconds=%.17g layout_seconds=%.17g pso_seconds=%.17g modules=%u peak_modules=%u psos=%u api_batches=%u emitted_bytes=%llu max_module_bytes=%llu inline_bank_bytes=%llu scratch_capacity_bytes=%llu lookups=%u\n",query_seconds,setup_seconds,elapsed,init_profile.module_seconds,init_profile.layout_seconds,init_profile.pipeline_seconds,init_profile.modules,init_profile.module_peak,init_profile.pipelines,init_profile.calls,(unsigned long long)init_profile.emitted_bytes,(unsigned long long)init_profile.max_module_bytes,(unsigned long long)sizeof(ready),(unsigned long long)(scratch.capacity*4),lookups);
 printf("INIT_FIRST_DRAW record_seconds=%.17g submit_complete_seconds=%.17g validation_seconds=%.17g gpu_seconds=%.17g primitives=%llu vertices=%llu fragments=%llu pixels=%u\n",first.record_seconds,first.submit_complete_seconds,first.validation_seconds,first.gpu_seconds,(unsigned long long)first.primitives,(unsigned long long)first.vertices,(unsigned long long)first.fragments,first.pixels);
 puts("PASS: complete supported bank, correct first transfer, pure lookups, all acquired API objects released; driver-allocation bytes unobserved");result=0;
done:
 if(g.device){if(ready.device && !xe_transfer_identity_vk_close(&ready,&ops))result=1;
  for(f=0;f<4;++f)for(d=0;d<3;++d)if(config.passes[f][d] && g.vkDestroyRenderPass)g.vkDestroyRenderPass(g.device,config.passes[f][d],NULL);
  if(images && g.vkDestroyDescriptorSetLayout)g.vkDestroyDescriptorSetLayout(g.device,images,NULL);
  if(vertex && g.vkDestroyShaderModule)g.vkDestroyShaderModule(g.device,vertex,NULL);
  if(g.vkDestroyDevice)g.vkDestroyDevice(g.device,NULL);}
 xe_spv_binary_close(&scratch);if(g.instance && g.vkDestroyInstance)g.vkDestroyInstance(g.instance,NULL);
#ifdef _WIN32
 if(loader)FreeLibrary(loader);
#else
 if(loader)dlclose(loader);
#endif
 return result;
}
