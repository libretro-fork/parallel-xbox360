/* Raw generic color images versus the unchanged legacy producer. C89.
 * Untimed correctness only: readbacks and reference PSOs are test machinery. */
#ifdef XE_COLOR_WARM_BENCH
#define XE_SHADER_COMPILE_BENCH
#endif
#include "transfer_uint_vulkan_image.h"
#include "libretro/core_transfer_color_vk.h"
#include "libretro/core_transfer_geometry.h"
typedef struct color_legacy_source { const char *directory;unsigned scale; } color_legacy_source;
static int color_emit_legacy(void *context,unsigned source,unsigned dest,
 unsigned dest_native,unsigned source_native,xe_spv_binary *output){
 static const unsigned formats[8]={0,1,2,3,4,5,14,15};
 const color_legacy_source *input=(const color_legacy_source*)context;
 unsigned s,d;char path[1024];FILE *file;long bytes;size_t count;int good;
 for(s=0;s<8 && formats[s]!=source;++s){}
 for(d=0;d<8 && formats[d]!=dest;++d){}
 if(!input || s==8 || d==8)return 0;
 if(strlen(input->directory)>sizeof(path)-96)return 0;
 sprintf(path,"%s/ref-g%u-c%u-s%u-d%u.spv",input->directory,input->scale,
  dest_native*2+source_native,s,d);
 file=fopen(path,"rb");if(!file)return 0;
 if(fseek(file,0,SEEK_END) || (bytes=ftell(file))<=0 || bytes%4 ||
    fseek(file,0,SEEK_SET)){fclose(file);return 0;}
 count=(size_t)bytes/4;xe_spv_binary_reset(output);
 if(!xe_spv_binary_reserve_exact(output,count)){fclose(file);return 0;}
 good=fread(output->words,4,count,file)==count && !ferror(file);
 if(fclose(file))good=0;
 if(good)output->count=count;
 return good;
}
static VkFormat color_view(unsigned i) {
 static const VkFormat views[8]={VK_FORMAT_R8G8B8A8_UNORM,VK_FORMAT_R16G16B16A16_UNORM,
  VK_FORMAT_A2B10G10R10_UNORM_PACK32,VK_FORMAT_R16G16B16A16_SFLOAT,
  VK_FORMAT_R16G16_UINT,VK_FORMAT_R16G16B16A16_UINT,VK_FORMAT_R32_UINT,VK_FORMAT_R32G32_UINT};
 return views[i];
}
static unsigned color_bytes(unsigned i){return i==1 || i==3 || i==5 || i==7?8:4;}
static uint16_t color_half7e3(unsigned code) {
 unsigned exponent=code>>7,mantissa=code&127,k;
 if(exponent)return (uint16_t)(((exponent+12)<<10)|(mantissa<<3));
 if(!mantissa)return 0;
 for(k=0;(1u<<(k+1))<=mantissa;++k){}
 return (uint16_t)(((k+6)<<10)|((mantissa-(1u<<k))<<(10-k)));
}
static void color_input(void *memory,unsigned view,unsigned width,unsigned height,unsigned seed) {
 static const uint32_t edges[8]={0,0x80000000,1,0x7fa12345,0x7fc12345,0xffc54321,0xffffffff,0x7f800000};
 static const uint16_t alpha[4]={0,0x3555,0x3955,0x3c00};
 uint32_t p,v;unsigned x,y,lane,components=view==4 || view==7?2:view==6?1:4;
 for(y=0;y<height;++y)for(x=0;x<width;++x){
  p=x+y*width+seed;
  if(view==2){((uint32_t*)memory)[y*width+x]=(p&1023)|(((p+341)&1023)<<10)|(((p+683)&1023)<<20)|((p&3)<<30);continue;}
  for(lane=0;lane<components;++lane){
   v=p+lane*8191;
   if(view==0)((unsigned char*)memory)[(y*width+x)*4+lane]=(unsigned char)(p+lane*61);
   else if(view==3)((uint16_t*)memory)[(y*width+x)*4+lane]=lane==3?alpha[p&3]:color_half7e3((v+lane*341)&1023);
   else if(view==1 || view==4 || view==5)((uint16_t*)memory)[(y*width+x)*components+lane]=(uint16_t)v;
   else ((uint32_t*)memory)[(y*width+x)*components+lane]=(v&15)<8?edges[v&7]:v*UINT32_C(0x9e3779b9)^(v<<13);
  }
 }
}
static int color_vertex_module(VkShaderModule *module) {
 xe_spv_binary code={0};VkShaderModuleCreateInfo info={0};VkResult status;
 if(!xe_transfer_rectangle_spv(&code))return 0;
 info.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;info.codeSize=code.count*4;info.pCode=code.words;
 status=g.vkCreateShaderModule(g.device,&info,NULL,module);xe_spv_binary_close(&code);return status==VK_SUCCESS;
}
static void color_begin_pass(VkCommandBuffer command,VkRenderPass pass,VkFramebuffer framebuffer,
 VkImageView view,unsigned width,unsigned height,unsigned dynamic){
 VkRenderPassBeginInfo render={0};VkClearValue clear={0};
 VkRenderingInfo rendering={0};VkRenderingAttachmentInfo attachment={0};
 if(dynamic){
  attachment.sType=VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;attachment.imageView=view;
  attachment.imageLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  attachment.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
  rendering.sType=VK_STRUCTURE_TYPE_RENDERING_INFO;rendering.renderArea.extent.width=width;
  rendering.renderArea.extent.height=height;rendering.layerCount=1;rendering.colorAttachmentCount=1;
  rendering.pColorAttachments=&attachment;im.begin_rendering(command,&rendering);
 }else{
  render.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;render.renderPass=pass;render.framebuffer=framebuffer;
  render.renderArea.extent.width=width;render.renderArea.extent.height=height;
  render.clearValueCount=1;render.pClearValues=&clear;g.vkCmdBeginRenderPass(command,&render,VK_SUBPASS_CONTENTS_INLINE);
 }
}
static void color_end_pass(VkCommandBuffer command,unsigned dynamic){
 if(dynamic)im.end_rendering(command);else g.vkCmdEndRenderPass(command);
}
#ifdef XE_COLOR_WARM_BENCH
#include "transfer_color_warm_profile.h"
#endif
int main(int argc,char **argv) {
#ifdef _WIN32
 HMODULE loader=LoadLibraryA("vulkan-1.dll");FARPROC entry;
#else
 void *loader=dlopen("libvulkan.so.1",RTLD_NOW|RTLD_LOCAL),*entry;
#endif
 static const unsigned formats[8]={0,1,2,3,4,5,14,15};
 static const unsigned scales[3][2]={{1,1},{3,5},{4,4}};
#ifdef XE_COLOR_WARM_BENCH
 enum {BYTES=10240*256*8};
 VkPhysicalDeviceFeatures features={0};PFN_vkGetPhysicalDeviceFeatures get_features;
 color_warm_case warm_case={0};
#else
 enum {BYTES=640*160*8};
#endif
 VkApplicationInfo app={0};VkInstanceCreateInfo instance={0};VkPhysicalDevice physical[8];
 VkPhysicalDeviceFeatures2 features2={0};VkPhysicalDeviceDynamicRenderingFeatures dynamic_features={0};
 VkPhysicalDeviceProperties properties;VkQueueFamilyProperties families[32];
 VkDeviceQueueCreateInfo queue_info={0};VkDeviceCreateInfo device_info={0};VkQueue queue;
 VkDescriptorSetLayoutBinding binding={0};VkDescriptorSetLayoutCreateInfo descriptor_info={0};VkDescriptorSetLayout descriptor=VK_NULL_HANDLE;
 VkPushConstantRange push={VK_SHADER_STAGE_FRAGMENT_BIT,0,32};VkPipelineLayoutCreateInfo layout_info={0};VkPipelineLayout legacy_layout=VK_NULL_HANDLE;
 VkDescriptorPoolSize pool_size={VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1};VkDescriptorPoolCreateInfo pool_info={0};VkDescriptorPool pool=VK_NULL_HANDLE;
 VkDescriptorSetAllocateInfo allocate_set={0};VkDescriptorSet set;VkDescriptorImageInfo image_info={0};VkWriteDescriptorSet write={0};
 VkShaderModule vertex_module=VK_NULL_HANDLE,legacy_module=VK_NULL_HANDLE;
 VkRenderPass render[8]={0};VkPipeline legacy_pipeline[3][4][8][8]={{{{0}}}};
 VkFramebuffer framebuffer=VK_NULL_HANDLE;VkPipeline active;VkPipelineLayout active_layout;
 VkPipelineShaderStageCreateInfo stages[2]={{0},{0}};VkVertexInputBindingDescription vb={0};VkVertexInputAttributeDescription va={0};
 VkPipelineVertexInputStateCreateInfo vertex={0};VkPipelineInputAssemblyStateCreateInfo assembly={0};
 VkPipelineViewportStateCreateInfo viewport_info={0};VkPipelineRasterizationStateCreateInfo raster={0};
 VkPipelineMultisampleStateCreateInfo msaa={0};VkPipelineColorBlendAttachmentState blend_attachment={0};VkPipelineColorBlendStateCreateInfo blend={0};
 VkDynamicState dynamics[2]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};VkPipelineDynamicStateCreateInfo dynamic={0};
 VkGraphicsPipelineCreateInfo pipeline_info={0};VkAttachmentDescription attachment={0};VkAttachmentReference reference={0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
 VkPipelineRenderingCreateInfo reference_rendering={0};VkFormat attachment_format;
 VkSubpassDescription subpass={0};VkRenderPassCreateInfo render_info={0};VkFramebufferCreateInfo framebuffer_info={0};
 VkCommandPoolCreateInfo command_info={0};VkCommandPool commands=VK_NULL_HANDLE;VkCommandBufferAllocateInfo allocate_command={0};VkCommandBuffer command;
 VkFenceCreateInfo fence_info={0};VkFence fence=VK_NULL_HANDLE;VkCommandBufferBeginInfo begin={0};VkSubmitInfo submit={0};
 VkViewport viewport={0};VkRect2D scissor={{0,0},{0,0}};
 VkBufferImageCopy copy={0};VkMemoryBarrier barrier={0};VkDeviceSize offset=0;
 struct buffer upload={0},readback[2]={{0}},vertices={0};image source={0},dest={0};
 xe_transfer_color_options options={0};xe_transfer_address_packet_config config={1,1,1};xe_transfer_address_key key={0};xe_transfer_color_packet packet;
 uint32_t count,family,legacy_packet;unsigned gidx,c,s,d,delta_case,variant,x,sw,sh,dw,dh,bytes,cases=0,identities=0;
 color_legacy_source legacy_source={0};
 xe_transfer_color_vk_bank banks[3]={{0}};xe_transfer_color_vk_config bank_config={0};
 xe_transfer_color_vk_ops ops={0};xe_spv_binary scratch={0};const VkPipeline *prepared;
 const char *rendering_mode=getenv("COLOR_DYNAMIC_RENDERING");
 float priority=1;int result=1;char path[4096];unsigned dynamic_rendering=rendering_mode && !strcmp(rendering_mode,"1");
 CHECK(loader && argc==3 && strlen(argv[1])<1900 && strlen(argv[2])<1900);
 CHECK(!rendering_mode || dynamic_rendering || !strcmp(rendering_mode,"0"));
#ifdef _WIN32
 entry=GetProcAddress(loader,"vkGetInstanceProcAddr");
#else
 entry=dlsym(loader,"vkGetInstanceProcAddr");
#endif
 memcpy(&g.get,&entry,sizeof(g.get));CHECK(g.get);LOAD(vkCreateInstance);
 app.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO;app.apiVersion=dynamic_rendering?VK_API_VERSION_1_3:VK_API_VERSION_1_0;
 instance.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;instance.pApplicationInfo=&app;OK(g.vkCreateInstance(&instance,NULL,&g.instance));
 LOAD(vkDestroyInstance);LOAD(vkEnumeratePhysicalDevices);LOAD(vkGetPhysicalDeviceProperties);LOAD(vkGetPhysicalDeviceMemoryProperties);LOAD(vkGetPhysicalDeviceQueueFamilyProperties);LOAD(vkCreateDevice);
 count=8;OK(g.vkEnumeratePhysicalDevices(g.instance,&count,physical));CHECK(count);
 g.vkGetPhysicalDeviceProperties(physical[0],&properties);g.vkGetPhysicalDeviceMemoryProperties(physical[0],&g.memory);
 fprintf(stderr,"COLOR_IMAGE_DEVICE %s driver%u\n",properties.deviceName,properties.driverVersion);
 count=32;g.vkGetPhysicalDeviceQueueFamilyProperties(physical[0],&count,families);
 for(family=0;family<count;++family)if(families[family].queueFlags&VK_QUEUE_GRAPHICS_BIT)break;
 CHECK(family<count);queue_info.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;queue_info.queueFamilyIndex=family;queue_info.queueCount=1;queue_info.pQueuePriorities=&priority;
 device_info.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;device_info.queueCreateInfoCount=1;device_info.pQueueCreateInfos=&queue_info;
 if(dynamic_rendering){
  LOAD(vkGetPhysicalDeviceFeatures2);
  features2.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;features2.pNext=&dynamic_features;
  dynamic_features.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
  g.vkGetPhysicalDeviceFeatures2(physical[0],&features2);CHECK(dynamic_features.dynamicRendering);
  device_info.pNext=&dynamic_features;
 }
#ifdef XE_COLOR_WARM_BENCH
 {PFN_vkVoidFunction f=g.get(g.instance,"vkGetPhysicalDeviceFeatures");memcpy(&get_features,&f,sizeof(f));CHECK(get_features);}
 get_features(physical[0],&features);CHECK(features.pipelineStatisticsQuery);
 memset(&features,0,sizeof(features));features.pipelineStatisticsQuery=VK_TRUE;device_info.pEnabledFeatures=&features;
#endif
 OK(g.vkCreateDevice(physical[0],&device_info,NULL,&g.device));
 LOAD(vkDestroyDevice);LOAD(vkGetDeviceQueue);LOAD(vkCreateBuffer);LOAD(vkDestroyBuffer);LOAD(vkGetBufferMemoryRequirements);LOAD(vkAllocateMemory);LOAD(vkFreeMemory);LOAD(vkBindBufferMemory);LOAD(vkMapMemory);LOAD(vkUnmapMemory);LOAD(vkFlushMappedMemoryRanges);LOAD(vkInvalidateMappedMemoryRanges);
 LOAD(vkCreateShaderModule);LOAD(vkDestroyShaderModule);LOAD(vkCreateDescriptorSetLayout);LOAD(vkDestroyDescriptorSetLayout);LOAD(vkCreatePipelineLayout);LOAD(vkDestroyPipelineLayout);LOAD(vkCreateRenderPass);LOAD(vkDestroyRenderPass);LOAD(vkCreateGraphicsPipelines);LOAD(vkDestroyPipeline);LOAD(vkCreateFramebuffer);LOAD(vkDestroyFramebuffer);
 LOAD(vkCreateDescriptorPool);LOAD(vkDestroyDescriptorPool);LOAD(vkAllocateDescriptorSets);LOAD(vkUpdateDescriptorSets);LOAD(vkCreateCommandPool);LOAD(vkDestroyCommandPool);LOAD(vkAllocateCommandBuffers);LOAD(vkResetCommandPool);LOAD(vkBeginCommandBuffer);LOAD(vkEndCommandBuffer);LOAD(vkCmdPipelineBarrier);LOAD(vkCmdBindPipeline);LOAD(vkCmdBindDescriptorSets);LOAD(vkCmdPushConstants);LOAD(vkCmdBindVertexBuffers);LOAD(vkCmdBeginRenderPass);LOAD(vkCmdEndRenderPass);LOAD(vkCmdDraw);LOAD(vkCreateFence);LOAD(vkDestroyFence);LOAD(vkResetFences);LOAD(vkQueueSubmit);LOAD(vkWaitForFences);
 IMAGE_FN(create,vkCreateImage);IMAGE_FN(destroy,vkDestroyImage);IMAGE_FN(requirements,vkGetImageMemoryRequirements);IMAGE_FN(bind,vkBindImageMemory);IMAGE_FN(view,vkCreateImageView);IMAGE_FN(close_view,vkDestroyImageView);IMAGE_FN(upload,vkCmdCopyBufferToImage);IMAGE_FN(readback,vkCmdCopyImageToBuffer);IMAGE_FN(viewport,vkCmdSetViewport);IMAGE_FN(scissor,vkCmdSetScissor);
 if(dynamic_rendering){IMAGE_FN(begin_rendering,vkCmdBeginRendering);IMAGE_FN(end_rendering,vkCmdEndRendering);}
 fprintf(stderr,"COLOR_DYNAMIC_RENDERING %u\n",dynamic_rendering);
 g.vkGetDeviceQueue(g.device,family,0,&queue);
 CHECK(make_host(&upload,BYTES) && make_host(&readback[0],BYTES) && make_host(&readback[1],BYTES) && make_host(&vertices,16));
 CHECK(readback[0].buffer!=readback[1].buffer && readback[0].mapped!=readback[1].mapped);
 {float *v=(float*)vertices.mapped;v[0]=v[1]=-1;v[2]=v[3]=1;}CHECK(cache_host(&vertices,0));CHECK(color_vertex_module(&vertex_module));
 binding.descriptorType=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;binding.descriptorCount=1;binding.stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT;
 descriptor_info.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;descriptor_info.bindingCount=1;descriptor_info.pBindings=&binding;OK(g.vkCreateDescriptorSetLayout(g.device,&descriptor_info,NULL,&descriptor));
 layout_info.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;layout_info.setLayoutCount=1;layout_info.pSetLayouts=&descriptor;layout_info.pushConstantRangeCount=1;layout_info.pPushConstantRanges=&push;
 push.size=4;OK(g.vkCreatePipelineLayout(g.device,&layout_info,NULL,&legacy_layout));
 pool_info.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;pool_info.maxSets=1;pool_info.poolSizeCount=1;pool_info.pPoolSizes=&pool_size;OK(g.vkCreateDescriptorPool(g.device,&pool_info,NULL,&pool));
 allocate_set.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;allocate_set.descriptorPool=pool;allocate_set.descriptorSetCount=1;allocate_set.pSetLayouts=&descriptor;OK(g.vkAllocateDescriptorSets(g.device,&allocate_set,&set));
 stages[0].sType=stages[1].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;stages[0].module=vertex_module;stages[0].pName=stages[1].pName="main";
 vb.stride=16;vb.inputRate=VK_VERTEX_INPUT_RATE_INSTANCE;va.format=VK_FORMAT_R32G32B32A32_SFLOAT;
 vertex.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;vertex.vertexBindingDescriptionCount=vertex.vertexAttributeDescriptionCount=1;vertex.pVertexBindingDescriptions=&vb;vertex.pVertexAttributeDescriptions=&va;
 assembly.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
 viewport_info.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;viewport_info.viewportCount=viewport_info.scissorCount=1;
 raster.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;raster.polygonMode=VK_POLYGON_MODE_FILL;raster.lineWidth=1;
 msaa.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;msaa.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
 blend_attachment.colorWriteMask=15;blend.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;blend.attachmentCount=1;blend.pAttachments=&blend_attachment;
 dynamic.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;dynamic.dynamicStateCount=2;dynamic.pDynamicStates=dynamics;
 pipeline_info.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;pipeline_info.stageCount=2;pipeline_info.pStages=stages;pipeline_info.pVertexInputState=&vertex;pipeline_info.pInputAssemblyState=&assembly;pipeline_info.pViewportState=&viewport_info;pipeline_info.pRasterizationState=&raster;pipeline_info.pMultisampleState=&msaa;pipeline_info.pColorBlendState=&blend;pipeline_info.pDynamicState=&dynamic;
 attachment.samples=VK_SAMPLE_COUNT_1_BIT;attachment.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;attachment.initialLayout=attachment.finalLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
 subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;subpass.colorAttachmentCount=1;subpass.pColorAttachments=&reference;
 render_info.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;render_info.attachmentCount=1;render_info.pAttachments=&attachment;render_info.subpassCount=1;render_info.pSubpasses=&subpass;
 for(d=0;d<8;++d){attachment.format=color_view(d);OK(g.vkCreateRenderPass(g.device,&render_info,NULL,&render[d]));}
 ops.create_shader=g.vkCreateShaderModule;ops.destroy_shader=g.vkDestroyShaderModule;
 ops.create_layout=g.vkCreatePipelineLayout;ops.destroy_layout=g.vkDestroyPipelineLayout;
 ops.create_pipelines=g.vkCreateGraphicsPipelines;ops.destroy_pipeline=g.vkDestroyPipeline;
 bank_config.device=g.device;bank_config.image_layout=descriptor;bank_config.vertex=vertex_module;
 bank_config.dynamic_rendering=dynamic_rendering;
 bank_config.enabled_views=255;bank_config.address.native2=1;
 if(getenv("COLOR_PACKED_BANK") && !strcmp(getenv("COLOR_PACKED_BANK"),"1")){
  legacy_source.directory=argv[2];bank_config.emit_packed=color_emit_legacy;
  bank_config.emit_context=&legacy_source;
 }
 for(d=0;d<8;++d)bank_config.passes[d]=render[d];
 for(gidx=0;gidx<3;++gidx){
  legacy_source.scale=gidx;
  bank_config.address.scale_x=scales[gidx][0];bank_config.address.scale_y=scales[gidx][1];
  XE_COMPILE_BEGIN();
  CHECK(xe_transfer_color_vk_init(banks+gidx,&bank_config,&ops,&scratch));
  XE_COMPILE_END("vulkan.color_bank.init",gidx);
 }
 xe_spv_binary_close(&scratch);
 for(gidx=0;gidx<3;++gidx)for(c=0;c<(gidx?4u:1u);++c)for(s=0;s<8;++s)for(d=0;d<8;++d){
  sprintf(path,"%s/ref-g%u-c%u-s%u-d%u.spv",argv[2],gidx,c,s,d);CHECK(shader(path,&legacy_module));
  stages[1].module=legacy_module;pipeline_info.layout=legacy_layout;pipeline_info.renderPass=dynamic_rendering?VK_NULL_HANDLE:render[d];
  if(dynamic_rendering){
   attachment_format=color_view(d);reference_rendering.sType=VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
   reference_rendering.colorAttachmentCount=1;reference_rendering.pColorAttachmentFormats=&attachment_format;
   pipeline_info.pNext=&reference_rendering;
  }
  OK(g.vkCreateGraphicsPipelines(g.device,VK_NULL_HANDLE,1,&pipeline_info,NULL,&legacy_pipeline[gidx][c][s][d]));
  g.vkDestroyShaderModule(g.device,legacy_module,NULL);legacy_module=VK_NULL_HANDLE;
 }
 command_info.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;command_info.queueFamilyIndex=family;OK(g.vkCreateCommandPool(g.device,&command_info,NULL,&commands));
 allocate_command.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;allocate_command.commandPool=commands;allocate_command.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;allocate_command.commandBufferCount=1;OK(g.vkAllocateCommandBuffers(g.device,&allocate_command,&command));
 fence_info.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;OK(g.vkCreateFence(g.device,&fence_info,NULL,&fence));
#ifdef XE_COLOR_WARM_BENCH
 CHECK(color_warm_init(commands,families[family].timestampValidBits,properties.limits.timestampPeriod));
 color_warm.dynamic_rendering=dynamic_rendering;
#endif
 key.source_color=key.dest_color=1;key.source_pitch=key.dest_pitch=2;key.source_native2=key.dest_native2=1;
 for(gidx=0;gidx<3;++gidx)for(c=0;c<(gidx?4u:1u);++c)for(s=0;s<8;++s)for(d=0;d<8;++d)for(delta_case=0;delta_case<3;++delta_case){
  if((s>=4 && d>=4) || s==d)continue;
#ifdef XE_COLOR_WARM_BENCH
  if(c || delta_case || (s>=4 && d>=4) || s==d)continue;
  key.source_pitch=key.dest_pitch=32;
#endif
  config.scale_x=scales[gidx][0];config.scale_y=scales[gidx][1];
  key.source_scale_x=c&1?1:config.scale_x;key.source_scale_y=c&1?1:config.scale_y;
  key.dest_scale_x=c&2?1:config.scale_x;key.dest_scale_y=c&2?1:config.scale_y;
  key.source_wide=s==5 || s==7;key.dest_wide=d==5 || d==7;
  key.dest_to_source_base=delta_case==1?1:delta_case==2?-1:0;
  options.source_uint=s>=4;options.dest_uint=d>=4;
  CHECK(xe_transfer_color_packet_prepare(&packet,&options,&config,&key,formats[s],formats[d],0));
  legacy_packet=key.dest_pitch|(key.source_pitch<<10)|(((uint32_t)key.dest_to_source_base&4095u)<<20);
  sw=(160*key.source_scale_x)>>key.source_wide;sh=32*key.source_scale_y;
  dw=(160*key.dest_scale_x)>>key.dest_wide;dh=32*key.dest_scale_y;
#ifdef XE_COLOR_WARM_BENCH
  sw=(2560*key.source_scale_x)>>key.source_wide;sh=64*key.source_scale_y;
  dw=(2560*key.dest_scale_x)>>key.dest_wide;dh=64*key.dest_scale_y;
#endif
  CHECK(make_image(&source,color_view(s),sw,sh,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT) && make_image(&dest,color_view(d),dw,dh,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT));
  color_input(upload.mapped,s,sw,sh,gidx*17+c*43+s*61+d*97);
  for(variant=0;variant<2;++variant)memset(readback[variant].mapped,0x6d,BYTES);
  CHECK(cache_host(&upload,0) && cache_host(&readback[0],0) && cache_host(&readback[1],0));
  framebuffer_info.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;framebuffer_info.renderPass=render[d];framebuffer_info.attachmentCount=1;framebuffer_info.pAttachments=&dest.view;framebuffer_info.width=dw;framebuffer_info.height=dh;framebuffer_info.layers=1;OK(g.vkCreateFramebuffer(g.device,&framebuffer_info,NULL,&framebuffer));
  image_info.imageView=source.view;image_info.imageLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  write.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;write.dstSet=set;write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;write.pImageInfo=&image_info;g.vkUpdateDescriptorSets(g.device,1,&write,0,NULL);
  OK(g.vkResetCommandPool(g.device,commands,0));begin.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;OK(g.vkBeginCommandBuffer(command,&begin));
  barrier.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;barrier.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
  g.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,0,1,&barrier,0,NULL,0,NULL);
  transition(command,source.image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,VK_ACCESS_TRANSFER_WRITE_BIT);
  copy.imageSubresource.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;copy.imageSubresource.layerCount=1;copy.imageExtent.width=sw;copy.imageExtent.height=sh;copy.imageExtent.depth=1;
  im.upload(command,upload.buffer,source.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
  transition(command,source.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
  for(variant=0;variant<2;++variant){
   prepared=xe_transfer_color_vk_get(banks+gidx,formats[s],formats[d],c>>1,c&1);CHECK(prepared && *prepared);
   active=variant?legacy_pipeline[gidx][c][s][d]:*prepared;active_layout=variant?legacy_layout:banks[gidx].layout;
   transition(command,dest.image,variant?VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    variant?VK_PIPELINE_STAGE_TRANSFER_BIT:VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,variant?VK_ACCESS_TRANSFER_READ_BIT:0,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
   color_begin_pass(command,render[d],framebuffer,dest.view,dw,dh,dynamic_rendering);
   viewport.width=(float)dw;viewport.height=(float)dh;viewport.maxDepth=1;scissor.offset.x=delta_case==2?(int32_t)((80*key.dest_scale_x)>>key.dest_wide):0;
   scissor.extent.width=delta_case?((80*key.dest_scale_x)>>key.dest_wide):dw;scissor.extent.height=delta_case?16*key.dest_scale_y:dh;
   im.viewport(command,0,1,&viewport);im.scissor(command,0,1,&scissor);g.vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,active);g.vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,active_layout,0,1,&set,0,NULL);g.vkCmdBindVertexBuffers(command,0,1,&vertices.buffer,&offset);
   if(variant || banks[gidx].packed_address)g.vkCmdPushConstants(command,active_layout,VK_SHADER_STAGE_FRAGMENT_BIT,0,4,&legacy_packet);
   else g.vkCmdPushConstants(command,active_layout,VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(packet),&packet);
   g.vkCmdDraw(command,6,1,0,0);color_end_pass(command,dynamic_rendering);
   transition(command,dest.image,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
   copy.imageExtent.width=dw;copy.imageExtent.height=dh;im.readback(command,dest.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,readback[variant].buffer,1,&copy);
  }
  barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;g.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,NULL,0,NULL);
  OK(g.vkEndCommandBuffer(command));OK(g.vkResetFences(g.device,1,&fence));submit.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;submit.commandBufferCount=1;submit.pCommandBuffers=&command;OK(g.vkQueueSubmit(queue,1,&submit,fence));
  if(g.vkWaitForFences(g.device,1,&fence,VK_TRUE,UINT64_C(5000000000))!=VK_SUCCESS){fprintf(stderr,"Pending color image test resources retained\n");exit(1);}
  CHECK(cache_host(&readback[0],1) && cache_host(&readback[1],1));bytes=dw*dh*color_bytes(d);
  for(x=0;x<bytes;++x)if(((unsigned char*)readback[0].mapped)[x]!=((unsigned char*)readback[1].mapped)[x]){
   fprintf(stderr,"Color image g%u native%u sf%u df%u delta%u byte%u: %02x != legacy %02x\n",gidx,c,formats[s],formats[d],delta_case,x,((unsigned char*)readback[0].mapped)[x],((unsigned char*)readback[1].mapped)[x]);goto done;
  }
  for(variant=0;variant<2;++variant)for(x=bytes;x<BYTES;++x)CHECK(((unsigned char*)readback[variant].mapped)[x]==0x6d);
  if(!delta_case && s==d && sw==dw && sh==dh){CHECK(!memcmp(upload.mapped,readback[0].mapped,bytes));++identities;}
#ifdef XE_COLOR_WARM_BENCH
  warm_case.pool=commands;warm_case.command=command;warm_case.queue=queue;warm_case.fence=fence;
  warm_case.pass=render[d];warm_case.framebuffer=framebuffer;warm_case.vertices=vertices.buffer;warm_case.descriptor=set;
  prepared=xe_transfer_color_vk_get(banks+gidx,formats[s],formats[d],c>>1,c&1);CHECK(prepared);
  warm_case.pipeline[0]=*prepared;warm_case.pipeline[1]=legacy_pipeline[gidx][c][s][d];
  warm_case.layout[0]=banks[gidx].layout;warm_case.layout[1]=legacy_layout;warm_case.dest=&dest;
  warm_case.same_pipeline=getenv("COLOR_SAME_PIPELINE") && !strcmp(getenv("COLOR_SAME_PIPELINE"),"1");
  if(warm_case.same_pipeline){warm_case.pipeline[1]=warm_case.pipeline[0];warm_case.layout[1]=warm_case.layout[0];}
  warm_case.packed_address=banks[gidx].packed_address;warm_case.packet=&packet;warm_case.legacy_packet=legacy_packet;warm_case.width=dw;warm_case.height=dh;
  warm_case.dynamic_rendering=dynamic_rendering;
  warm_case.scale=gidx;warm_case.source_format=formats[s];warm_case.dest_format=formats[d];
  CHECK(color_warm_profile(&warm_case));
#endif
  g.vkDestroyFramebuffer(g.device,framebuffer,NULL);framebuffer=VK_NULL_HANDLE;close_image(&source);close_image(&dest);++cases;
 }
#ifdef XE_COLOR_WARM_BENCH
 CHECK(cases==132 && !identities);
 printf("PASS: %u exact large-image color pairs before GPU profiles; timing acceptance requires raw-sample analysis\n",cases);
#else
 CHECK(cases==1188 && !identities);
 printf("PASS: %u SS color image pairs match legacy exactly, all44 selected typed pairs/9 native-scale classes/zero and signed base deltas and tail guards; existing raw/identity banks retain priority, MSAA coverage pending\n",cases);
#endif
 result=0;
done:
 if(g.device){
#ifdef XE_COLOR_WARM_BENCH
  color_warm_close();
#endif
  if(framebuffer)g.vkDestroyFramebuffer(g.device,framebuffer,NULL);
  close_image(&source);close_image(&dest);
  if(fence)g.vkDestroyFence(g.device,fence,NULL);
  if(commands)g.vkDestroyCommandPool(g.device,commands,NULL);
  for(gidx=0;gidx<3;++gidx){
   CHECK(xe_transfer_color_vk_close(banks+gidx,&ops));
   for(c=0;c<4;++c)for(s=0;s<8;++s)for(d=0;d<8;++d)if(legacy_pipeline[gidx][c][s][d])g.vkDestroyPipeline(g.device,legacy_pipeline[gidx][c][s][d],NULL);
  }
  for(d=0;d<8;++d)if(render[d])g.vkDestroyRenderPass(g.device,render[d],NULL);
  if(legacy_module)g.vkDestroyShaderModule(g.device,legacy_module,NULL);
  if(vertex_module)g.vkDestroyShaderModule(g.device,vertex_module,NULL);
  if(pool)g.vkDestroyDescriptorPool(g.device,pool,NULL);

  if(legacy_layout)g.vkDestroyPipelineLayout(g.device,legacy_layout,NULL);
  if(descriptor)g.vkDestroyDescriptorSetLayout(g.device,descriptor,NULL);
  close_host(&vertices);close_host(&upload);close_host(&readback[0]);close_host(&readback[1]);if(g.vkDestroyDevice)g.vkDestroyDevice(g.device,NULL);
 }
 if(g.instance && g.vkDestroyInstance)g.vkDestroyInstance(g.instance,NULL);
#ifdef _WIN32
 if(loader)FreeLibrary(loader);
#else
 if(loader)dlclose(loader);
#endif
 xe_spv_binary_close(&scratch);
 return result;
}
