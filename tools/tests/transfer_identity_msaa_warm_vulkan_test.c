/* Matched typed MSAA throughput, independent images and work counters.
 * BSD; MSVC C89. Small mode is diagnostic only, never throughput acceptance. */
#ifdef XE_IDENTITY_MSAA_SMALL
#define IDENTITY_TILES_X 1
#define IDENTITY_TILES_Y 1
#else
#define IDENTITY_TILES_X 16
#define IDENTITY_TILES_Y 8
#endif
#include "transfer_identity_msaa_vulkan_views.h"
#include "transfer_identity_msaa_cases.h"
#include "libretro/core_transfer_identity_vk.h"
typedef struct bank {
 VkPipelineLayout fill_layout,probe_layout;
 VkShaderModule vertex,fill,probe[2];
 VkRenderPass render[4][3],numeric_render[4][3];
 VkPipeline fill_pso[4][3][4],probe_pso[2];
 xe_transfer_identity_vk_bank ready[2][2][4];
} bank;
static unsigned sample_count(unsigned index){return 1u<<index;}
static unsigned format_index(unsigned f){return f;}
static int load_modules(bank *b,const char *prefix) {
 char path[4096];unsigned s;
 sprintf(path,"%s-vertex.spv",prefix);if(!shader(path,&b->vertex))return 0;
 sprintf(path,"%s-fill.spv",prefix);if(!shader(path,&b->fill))return 0;
 for(s=0;s<2;++s) {
  sprintf(path,"%s-probe-s%u.spv",prefix,s);if(!shader(path,&b->probe[s]))return 0;

 }
 return 1;
}
static void bank_ops(xe_transfer_identity_vk_ops *ops) {
 ops->create_shader=g.vkCreateShaderModule;ops->destroy_shader=g.vkDestroyShaderModule;
 ops->create_layout=g.vkCreatePipelineLayout;ops->destroy_layout=g.vkDestroyPipelineLayout;
 ops->create_pipelines=g.vkCreateGraphicsPipelines;ops->destroy_pipeline=g.vkDestroyPipeline;
}
static int make_bank(bank *b,VkDescriptorSetLayout image_set,VkDescriptorSetLayout probe_set,
 int native2,int sample_shading,int dynamic_rendering) {
 VkPipelineLayoutCreateInfo layout={0};VkPushConstantRange push={VK_SHADER_STAGE_FRAGMENT_BIT,0,20};
 VkAttachmentDescription attachment={0};VkAttachmentReference reference={0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
 VkSubpassDescription subpass={0};VkRenderPassCreateInfo render={0};
 VkPipelineShaderStageCreateInfo stages[2]={{0},{0}};VkVertexInputBindingDescription binding={0};VkVertexInputAttributeDescription attribute={0};
 VkPipelineVertexInputStateCreateInfo vertex={0};VkPipelineInputAssemblyStateCreateInfo assembly={0};VkPipelineViewportStateCreateInfo viewport={0};
 VkPipelineRasterizationStateCreateInfo raster={0};VkPipelineMultisampleStateCreateInfo msaa={0};
 VkPipelineColorBlendAttachmentState blend_attachment={0};VkPipelineColorBlendStateCreateInfo blend={0};
 VkDynamicState dynamic_values[2]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};VkPipelineDynamicStateCreateInfo dynamic={0};
 VkGraphicsPipelineCreateInfo pipeline={0};VkComputePipelineCreateInfo compute={0};
 unsigned f,c,s,n,kind,scale;VkSampleMask mask=1;
 xe_spv_binary scratch={0};xe_transfer_identity_vk_config config={0};xe_transfer_identity_vk_ops ops;
 layout.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;layout.pushConstantRangeCount=1;layout.pPushConstantRanges=&push;
 layout.setLayoutCount=0;push.size=16;if(g.vkCreatePipelineLayout(g.device,&layout,NULL,&b->fill_layout)!=VK_SUCCESS)return 0;
 layout.setLayoutCount=1;layout.pSetLayouts=&probe_set;push.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;
 if(g.vkCreatePipelineLayout(g.device,&layout,NULL,&b->probe_layout)!=VK_SUCCESS)return 0;
 stages[0].sType=stages[1].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[0].module=b->vertex;stages[0].pName=stages[1].pName="main";stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;
 binding.stride=16;binding.inputRate=VK_VERTEX_INPUT_RATE_INSTANCE;attribute.format=VK_FORMAT_R32G32B32A32_SFLOAT;
 vertex.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;vertex.vertexBindingDescriptionCount=vertex.vertexAttributeDescriptionCount=1;vertex.pVertexBindingDescriptions=&binding;vertex.pVertexAttributeDescriptions=&attribute;
 assembly.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
 viewport.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;viewport.viewportCount=viewport.scissorCount=1;
 raster.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;raster.polygonMode=VK_POLYGON_MODE_FILL;raster.lineWidth=1;
 msaa.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;msaa.pSampleMask=&mask;
 blend_attachment.colorWriteMask=15;blend.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;blend.attachmentCount=1;blend.pAttachments=&blend_attachment;
 dynamic.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;dynamic.dynamicStateCount=2;dynamic.pDynamicStates=dynamic_values;
 pipeline.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;pipeline.stageCount=2;pipeline.pStages=stages;pipeline.pVertexInputState=&vertex;pipeline.pInputAssemblyState=&assembly;pipeline.pViewportState=&viewport;pipeline.pRasterizationState=&raster;pipeline.pMultisampleState=&msaa;pipeline.pColorBlendState=&blend;pipeline.pDynamicState=&dynamic;
 attachment.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;attachment.initialLayout=attachment.finalLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
 subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;subpass.colorAttachmentCount=1;subpass.pColorAttachments=&reference;
 render.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;render.attachmentCount=1;render.pAttachments=&attachment;render.subpassCount=1;render.pSubpasses=&subpass;
 for(f=0;f<4;++f)for(c=0;c<3;++c) {
  if(c==1 && !native2)continue;
  attachment.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;attachment.format=identity_ms_integer_format(f);attachment.samples=(VkSampleCountFlagBits)sample_count(c);msaa.rasterizationSamples=attachment.samples;
  if(g.vkCreateRenderPass(g.device,&render,NULL,&b->render[f][c])!=VK_SUCCESS)return 0;
  pipeline.renderPass=b->render[f][c];pipeline.layout=b->fill_layout;stages[1].module=b->fill;msaa.sampleShadingEnable=VK_FALSE;
  for(s=0;s<sample_count(c);++s) {mask=1u<<s;if(g.vkCreateGraphicsPipelines(g.device,VK_NULL_HANDLE,1,&pipeline,NULL,&b->fill_pso[f][c][s])!=VK_SUCCESS)return 0;}
  attachment.format=identity_ms_numeric_format(f);attachment.loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;
  if(g.vkCreateRenderPass(g.device,&render,NULL,&b->numeric_render[f][c])!=VK_SUCCESS)return 0;
 }
 compute.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;compute.layout=b->probe_layout;compute.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;compute.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;compute.stage.pName="main";
 for(s=0;s<2;++s){compute.stage.module=b->probe[s];if(g.vkCreateComputePipelines(g.device,VK_NULL_HANDLE,1,&compute,NULL,&b->probe_pso[s])!=VK_SUCCESS)return 0;}
 bank_ops(&ops);config.device=g.device;config.image_layout=image_set;config.vertex=b->vertex;config.dynamic_rendering=(unsigned)dynamic_rendering;
 for(n=0;n<(native2?2u:1u);++n)for(kind=0;kind<2;++kind)for(scale=0;scale<3;++scale) {
  if(kind==0 && !sample_shading)continue;
  config.native2=n;config.sample_shading=kind==0;
  config.scale_x=config.scale_y=1u<<scale;
  for(f=0;f<4;++f)for(c=0;c<3;++c)config.passes[f][c]=b->numeric_render[f][c==1 && !n?2:c];
  for(f=0;f<4;++f)config.sample_mask[f]=7;
  XE_COMPILE_BEGIN();
  if(!xe_transfer_identity_vk_init(&b->ready[n][kind][scale],&config,&ops,&scratch)){xe_spv_binary_close(&scratch);return 0;}
  XE_COMPILE_END("vulkan.identity_bank.init",(n*2+kind)*4+scale);
 }
 xe_spv_binary_close(&scratch);
 return 1;
}
static void close_bank(bank *b) {
 unsigned f,c,s,n,kind,scale;xe_transfer_identity_vk_ops ops;bank_ops(&ops);
 for(n=0;n<2;++n)for(kind=0;kind<2;++kind)
 for(scale=0;scale<sizeof(b->ready[0][0])/sizeof(b->ready[0][0][0]);++scale)
  xe_transfer_identity_vk_close(&b->ready[n][kind][scale],&ops);
 for(f=0;f<4;++f)for(c=0;c<3;++c) {
  for(s=0;s<4;++s)if(b->fill_pso[f][c][s])g.vkDestroyPipeline(g.device,b->fill_pso[f][c][s],NULL);

  if(b->render[f][c])g.vkDestroyRenderPass(g.device,b->render[f][c],NULL);
  if(b->numeric_render[f][c])g.vkDestroyRenderPass(g.device,b->numeric_render[f][c],NULL);
 }
 for(s=0;s<2;++s){if(b->probe_pso[s])g.vkDestroyPipeline(g.device,b->probe_pso[s],NULL);if(b->probe[s])g.vkDestroyShaderModule(g.device,b->probe[s],NULL);}
 if(b->vertex)g.vkDestroyShaderModule(g.device,b->vertex,NULL);
 if(b->fill)g.vkDestroyShaderModule(g.device,b->fill,NULL);
 if(b->fill_layout)g.vkDestroyPipelineLayout(g.device,b->fill_layout,NULL);
 if(b->probe_layout)g.vkDestroyPipelineLayout(g.device,b->probe_layout,NULL);
}
static VkResult framebuffer(VkRenderPass render,VkImageView view,unsigned w,unsigned h,VkFramebuffer *out) {
 VkFramebufferCreateInfo info={0};info.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;info.renderPass=render;info.attachmentCount=1;info.pAttachments=&view;info.width=w;info.height=h;info.layers=1;
 return g.vkCreateFramebuffer(g.device,&info,NULL,out);
}
static void begin_image(VkCommandBuffer command,VkRenderPass render,VkFramebuffer framebuffer_,unsigned w,unsigned h,unsigned view) {
 VkRenderPassBeginInfo begin={0};VkClearValue clear={0};VkViewport viewport={0};VkRect2D scissor={{0,0},{0,0}};
 clear.color.uint32[0]=clear.color.uint32[1]=clear.color.uint32[2]=(7&identity_ms_mask(view))|identity_ms_base(view);
 clear.color.uint32[3]=(7&identity_ms_alpha_mask(view))|identity_ms_base(view);
 begin.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;begin.renderPass=render;begin.framebuffer=framebuffer_;begin.renderArea.extent.width=w;begin.renderArea.extent.height=h;begin.clearValueCount=1;begin.pClearValues=&clear;
 g.vkCmdBeginRenderPass(command,&begin,VK_SUBPASS_CONTENTS_INLINE);viewport.width=(float)w;viewport.height=(float)h;viewport.maxDepth=1;scissor.extent.width=w;scissor.extent.height=h;im.viewport(command,0,1,&viewport);im.scissor(command,0,1,&scissor);
}
static void begin_dynamic_image(VkCommandBuffer command,VkImageView view,unsigned w,unsigned h) {
 VkRenderingInfo info={0};VkRenderingAttachmentInfo color={0};VkViewport viewport={0};VkRect2D scissor={{0,0},{0,0}};
 color.sType=VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;color.imageView=view;color.imageLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;color.loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;color.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
 color.clearValue.color.uint32[0]=color.clearValue.color.uint32[1]=color.clearValue.color.uint32[2]=color.clearValue.color.uint32[3]=7;
 info.sType=VK_STRUCTURE_TYPE_RENDERING_INFO;info.renderArea.extent.width=w;info.renderArea.extent.height=h;info.layerCount=1;info.colorAttachmentCount=1;info.pColorAttachments=&color;
 im.begin_rendering(command,&info);viewport.width=(float)w;viewport.height=(float)h;viewport.maxDepth=1;scissor.extent.width=w;scissor.extent.height=h;im.viewport(command,0,1,&viewport);im.scissor(command,0,1,&scissor);
}
#include "transfer_identity_msaa_legacy_full_vk.h"
#include "transfer_identity_msaa_warm_probe_vk.h"
#include "transfer_identity_msaa_warm_vk.h"
int main(int argc,char **argv) {
#ifdef _WIN32
 HMODULE loader=LoadLibraryA("vulkan-1.dll");FARPROC entry;
#else
 void *loader=dlopen("libvulkan.so.1",RTLD_NOW|RTLD_LOCAL),*entry;
#endif
 static const unsigned formats[]={0,1,2,3};
 static const unsigned scales[3][4]={{1,1,1,1},{2,2,2,2},{4,4,4,4}};
 enum {BYTES=IDENTITY_TILES_X*IDENTITY_TILES_Y*80*16*16*2*16+256};
 VkApplicationInfo app={0};VkInstanceCreateInfo instance={0};VkPhysicalDevice physical[8];VkPhysicalDeviceProperties properties;VkPhysicalDeviceFeatures features={0},enabled={0};VkImageFormatProperties image_properties;
 VkPhysicalDeviceDynamicRenderingFeatures dynamic_features={0};VkPhysicalDeviceFeatures2 features2={0};
 VkQueueFamilyProperties families[32];VkDeviceQueueCreateInfo queue_info={0};VkDeviceCreateInfo device_info={0};VkQueue queue;float priority=1;
 VkDescriptorSetLayoutBinding bindings[2]={{0},{0}};VkDescriptorSetLayoutCreateInfo set_info={0};VkDescriptorSetLayout image_layout=VK_NULL_HANDLE,probe_layout=VK_NULL_HANDLE;
 VkDescriptorPoolSize sizes[2]={{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,2},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1}};VkDescriptorPoolCreateInfo pool_info={0};VkDescriptorPool pool=VK_NULL_HANDLE;
 VkDescriptorSetLayout layouts[2];VkDescriptorSet sets[2];VkDescriptorSetAllocateInfo allocate_sets={0};VkDescriptorImageInfo image_infos[2]={{0},{0}};VkDescriptorBufferInfo buffer_info={0};VkWriteDescriptorSet writes[3]={{0},{0},{0}};
 VkCommandPoolCreateInfo command_info={0};VkCommandPool commands=VK_NULL_HANDLE;VkCommandBufferAllocateInfo allocate_command={0};VkCommandBuffer command;
 VkFenceCreateInfo fence_info={0};VkFence fence=VK_NULL_HANDLE;VkCommandBufferBeginInfo begin={0};VkSubmitInfo submit={0};VkMemoryBarrier barrier={0};VkDeviceSize offset=0;
 identity_warm_case timed={0};identity_msaa_probe_context probe={0};
 bank b={0};identity_ms_image source={0},dest={0};VkFramebuffer source_fb=VK_NULL_HANDLE,dest_fb=VK_NULL_HANDLE,numeric_fb=VK_NULL_HANDLE;struct buffer readback={0},vertices={0};
 image typed_source={0},typed_dest={0};PFN_vkCmdCopyImage copy_image=NULL;PFN_vkVoidFunction copy_entry;
 VkImageUsageFlags copy_usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
 xe_transfer_address_key key={0};xe_transfer_identity_options options={0};xe_transfer_identity_packet packet;
 uint32_t count,family,sample_flags=UINT32_MAX,constants[4],wanted[4],*pixels;
 unsigned f,s,dm,sm,n,c,mode,si,di,samples,source_samples,sw,sh,dw,dh,x,y,lane,physical_sample,index,logical,records,cases=0,manual_cases=0,builtin_cases=0,single_cases=0;
 int native2=0,shading=0,result=1,dynamic_rendering=getenv("MSAA_DYNAMIC_RENDERING")!=NULL;
 int bad_sample=getenv("IDENTITY_MSAA_BAD_SAMPLE")!=NULL;
 int bad_scale=getenv("IDENTITY_MSAA_BAD_SCALE")!=NULL;
 int bad_mask=getenv("IDENTITY_MSAA_BAD_MASK")!=NULL;
 int typed_resources=1;const char *legacy_prefix=getenv("IDENTITY_LEGACY_PREFIX");
 unsigned variant,gx,gy;VkPipelineLayout active_layout;const VkPipeline *active_pipelines;uint32_t packed;
 const xe_transfer_identity_vk_bank *ready;const VkPipeline *ready_pipelines;
 CHECK(loader && argc==2 && strlen(argv[1])<2048);
 CHECK(legacy_prefix && strlen(legacy_prefix)<2048 && !bad_sample && !bad_scale && !bad_mask);
 fprintf(stderr,"IDENTITY_MSAA_CONTROL sample%d scale%d mask%d\n",bad_sample,bad_scale,bad_mask);
#ifdef _WIN32
 entry=GetProcAddress(loader,"vkGetInstanceProcAddr");
#else
 entry=dlsym(loader,"vkGetInstanceProcAddr");
#endif
 memcpy(&g.get,&entry,sizeof(g.get));CHECK(g.get);LOAD(vkCreateInstance);
 app.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO;app.apiVersion=dynamic_rendering?VK_API_VERSION_1_3:VK_API_VERSION_1_0;instance.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;instance.pApplicationInfo=&app;OK(g.vkCreateInstance(&instance,NULL,&g.instance));
 LOAD(vkDestroyInstance);LOAD(vkEnumeratePhysicalDevices);LOAD(vkGetPhysicalDeviceProperties);LOAD(vkGetPhysicalDeviceMemoryProperties);LOAD(vkGetPhysicalDeviceQueueFamilyProperties);LOAD(vkCreateDevice);IMAGE_FN(features,vkGetPhysicalDeviceFeatures);IMAGE_FN(properties,vkGetPhysicalDeviceImageFormatProperties);
 count=8;OK(g.vkEnumeratePhysicalDevices(g.instance,&count,physical));CHECK(count);g.vkGetPhysicalDeviceProperties(physical[0],&properties);g.vkGetPhysicalDeviceMemoryProperties(physical[0],&g.memory);im.features(physical[0],&features);
 CHECK(features.pipelineStatisticsQuery && properties.limits.maxStorageBufferRange>=BYTES);enabled.pipelineStatisticsQuery=VK_TRUE;
 enabled.sampleRateShading=features.sampleRateShading && !getenv("MSAA_DISABLE_SAMPLE_SHADING");shading=enabled.sampleRateShading!=0;
 if(dynamic_rendering) {
  CHECK(properties.apiVersion>=VK_API_VERSION_1_3);LOAD(vkGetPhysicalDeviceFeatures2);
  dynamic_features.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;features2.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;features2.pNext=&dynamic_features;g.vkGetPhysicalDeviceFeatures2(physical[0],&features2);CHECK(dynamic_features.dynamicRendering);device_info.pNext=&dynamic_features;
 }
 for(f=0;f<4;++f){OK(im.properties(physical[0],identity_ms_numeric_format(f),VK_IMAGE_TYPE_2D,VK_IMAGE_TILING_OPTIMAL,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT,&image_properties));sample_flags&=image_properties.sampleCounts;OK(im.properties(physical[0],identity_ms_integer_format(f),VK_IMAGE_TYPE_2D,VK_IMAGE_TILING_OPTIMAL,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT,&image_properties));sample_flags&=image_properties.sampleCounts;}
 CHECK((sample_flags&5)==5);native2=(sample_flags&2)!=0;
 fprintf(stderr,"IDENTITY_MSAA_DEVICE %s driver%u native2=%d sampleShading=%d dynamicRendering=%d typedResources=%d\n",properties.deviceName,properties.driverVersion,native2,shading,dynamic_rendering,typed_resources);
 count=32;g.vkGetPhysicalDeviceQueueFamilyProperties(physical[0],&count,families);for(family=0;family<count;++family)if((families[family].queueFlags&(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT))==(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT))break;CHECK(family<count);
 queue_info.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;queue_info.queueFamilyIndex=family;queue_info.queueCount=1;queue_info.pQueuePriorities=&priority;device_info.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;device_info.queueCreateInfoCount=1;device_info.pQueueCreateInfos=&queue_info;device_info.pEnabledFeatures=&enabled;OK(g.vkCreateDevice(physical[0],&device_info,NULL,&g.device));
 LOAD(vkDestroyDevice);LOAD(vkGetDeviceQueue);LOAD(vkCreateBuffer);LOAD(vkDestroyBuffer);LOAD(vkGetBufferMemoryRequirements);LOAD(vkAllocateMemory);LOAD(vkFreeMemory);LOAD(vkBindBufferMemory);LOAD(vkMapMemory);LOAD(vkUnmapMemory);LOAD(vkFlushMappedMemoryRanges);LOAD(vkInvalidateMappedMemoryRanges);LOAD(vkCreateShaderModule);LOAD(vkDestroyShaderModule);LOAD(vkCreateDescriptorSetLayout);LOAD(vkDestroyDescriptorSetLayout);LOAD(vkCreatePipelineLayout);LOAD(vkDestroyPipelineLayout);LOAD(vkCreateGraphicsPipelines);LOAD(vkCreateComputePipelines);LOAD(vkDestroyPipeline);LOAD(vkCreateRenderPass);LOAD(vkDestroyRenderPass);LOAD(vkCreateFramebuffer);LOAD(vkDestroyFramebuffer);
 LOAD(vkCreateDescriptorPool);LOAD(vkDestroyDescriptorPool);LOAD(vkAllocateDescriptorSets);LOAD(vkUpdateDescriptorSets);LOAD(vkCreateCommandPool);LOAD(vkDestroyCommandPool);LOAD(vkAllocateCommandBuffers);LOAD(vkResetCommandPool);LOAD(vkBeginCommandBuffer);LOAD(vkEndCommandBuffer);LOAD(vkCmdPipelineBarrier);LOAD(vkCmdBindPipeline);LOAD(vkCmdBindDescriptorSets);LOAD(vkCmdPushConstants);LOAD(vkCmdBindVertexBuffers);LOAD(vkCmdBeginRenderPass);LOAD(vkCmdEndRenderPass);LOAD(vkCmdDraw);LOAD(vkCmdDispatch);LOAD(vkCreateFence);LOAD(vkDestroyFence);LOAD(vkResetFences);LOAD(vkQueueSubmit);LOAD(vkWaitForFences);
 IMAGE_FN(create,vkCreateImage);IMAGE_FN(destroy,vkDestroyImage);IMAGE_FN(requirements,vkGetImageMemoryRequirements);IMAGE_FN(bind,vkBindImageMemory);IMAGE_FN(view,vkCreateImageView);IMAGE_FN(close_view,vkDestroyImageView);IMAGE_FN(viewport,vkCmdSetViewport);IMAGE_FN(scissor,vkCmdSetScissor);
 if(typed_resources){copy_entry=g.get(g.instance,"vkCmdCopyImage");memcpy(&copy_image,&copy_entry,sizeof(copy_image));CHECK(copy_image);}
 if(dynamic_rendering){IMAGE_FN(begin_rendering,vkCmdBeginRendering);IMAGE_FN(end_rendering,vkCmdEndRendering);}
 g.vkGetDeviceQueue(g.device,family,0,&queue);CHECK(make_host(&readback,BYTES) && make_host(&vertices,16));{float *v=(float*)vertices.mapped;v[0]=v[1]=-1;v[2]=v[3]=1;}CHECK(cache_host(&vertices,0));
 bindings[0].descriptorType=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;bindings[0].descriptorCount=1;bindings[0].stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT;set_info.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;set_info.bindingCount=1;set_info.pBindings=bindings;OK(g.vkCreateDescriptorSetLayout(g.device,&set_info,NULL,&image_layout));
 bindings[0].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;bindings[1].binding=1;bindings[1].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;bindings[1].descriptorCount=1;bindings[1].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;set_info.bindingCount=2;OK(g.vkCreateDescriptorSetLayout(g.device,&set_info,NULL,&probe_layout));
 CHECK(load_modules(&b,argv[1]) && make_bank(&b,image_layout,probe_layout,native2,shading,dynamic_rendering));
 CHECK(identity_legacy_init(&b,image_layout,legacy_prefix,(unsigned)native2,(unsigned)shading,(unsigned)dynamic_rendering));
 pool_info.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;pool_info.maxSets=2;pool_info.poolSizeCount=2;pool_info.pPoolSizes=sizes;OK(g.vkCreateDescriptorPool(g.device,&pool_info,NULL,&pool));layouts[0]=image_layout;layouts[1]=probe_layout;allocate_sets.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;allocate_sets.descriptorPool=pool;allocate_sets.descriptorSetCount=2;allocate_sets.pSetLayouts=layouts;OK(g.vkAllocateDescriptorSets(g.device,&allocate_sets,sets));
 command_info.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;command_info.queueFamilyIndex=family;OK(g.vkCreateCommandPool(g.device,&command_info,NULL,&commands));allocate_command.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;allocate_command.commandPool=commands;allocate_command.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;allocate_command.commandBufferCount=1;OK(g.vkAllocateCommandBuffers(g.device,&allocate_command,&command));fence_info.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;OK(g.vkCreateFence(g.device,&fence_info,NULL,&fence));
 CHECK(identity_warm_init(commands,families[family].timestampValidBits,properties.limits.timestampPeriod));
 key.source_color=key.dest_color=1;key.source_pitch=key.dest_pitch=IDENTITY_TILES_X;
 for(n=0;n<2;++n)for(s=0;s<4;++s)for(dm=0;dm<3;++dm)for(sm=0;sm<3;++sm)for(c=0;c<3;++c)for(mode=0;mode<(dm?2u:1u);++mode)for(variant=0;variant<2;++variant) {
  if((n && !native2) || (dm && !shading && mode==0))continue;
  key.source_msaa=sm;key.dest_msaa=dm;key.source_native2=key.dest_native2=n;key.source_wide=key.dest_wide=0;options.view=s;
  key.dest_scale_x=scales[c][0];key.dest_scale_y=scales[c][1];key.source_scale_x=scales[c][2];key.source_scale_y=scales[c][3];options.source_multisampled=sm!=0;options.sample_mode=dm?(mode?XE_TRANSFER_IDENTITY_SAMPLE_CONSTANT:XE_TRANSFER_IDENTITY_SAMPLE_BUILTIN):XE_TRANSFER_IDENTITY_SINGLE_SAMPLE;
  CHECK(xe_transfer_identity_packet_prepare(&packet,&options,&key,formats[s],formats[s],s==1,0));
  ready=&b.ready[n][mode || !shading?1:0][c];
  /* Test-only, legal in-bounds wrong shape: source becomes native while the
   * actual image remains scaled. It must not pass the independent oracle. */
  ready_pipelines=xe_transfer_identity_vk_get(ready,formats[s],dm,sm,0,
   0);CHECK(ready_pipelines);
  active_layout=variant?identity_legacy.layout:ready->layout;
  active_pipelines=variant?identity_legacy.pipeline[n][s][dm][sm][c][mode]:ready_pipelines;
  packed=key.dest_pitch|(key.source_pitch<<10)|(((uint32_t)key.dest_to_source_base&4095)<<20);
  sw=(80*IDENTITY_TILES_X*key.source_scale_x)>>(sm==2);sh=(16*IDENTITY_TILES_Y*key.source_scale_y)>>(sm!=0);
  dw=(80*IDENTITY_TILES_X*key.dest_scale_x)>>(dm==2);dh=(16*IDENTITY_TILES_Y*key.dest_scale_y)>>(dm!=0);
  si=sm==0?0:sm==1 && n?1:2;di=dm==0?0:dm==1 && n?1:2;source_samples=sample_count(si);samples=sample_count(di);
  CHECK(identity_ms_make_image_usage(&source,s,sw,sh,source_samples,typed_resources?copy_usage:0) &&
   identity_ms_make_image_usage(&dest,s,dw,dh,samples,typed_resources?copy_usage:0));
  if(typed_resources) {
   CHECK(make_image_samples(&typed_source,identity_ms_numeric_format(s),sw,sh,copy_usage|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,source_samples) &&
    make_image_samples(&typed_dest,identity_ms_numeric_format(s),dw,dh,copy_usage|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,samples));
  }
  OK(framebuffer(b.render[s][si],source.raw.view,sw,sh,&source_fb));OK(framebuffer(b.render[s][di],dest.raw.view,dw,dh,&dest_fb));if(!dynamic_rendering)OK(framebuffer(b.numeric_render[s][di],typed_resources?typed_dest.view:dest.numeric,dw,dh,&numeric_fb));
  memset(readback.mapped,0x6d,BYTES);CHECK(cache_host(&readback,0));
  image_infos[0].imageView=typed_resources?typed_source.view:source.numeric;image_infos[0].imageLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;image_infos[1].imageView=dest.raw.view;image_infos[1].imageLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  for(index=0;index<2;++index){writes[index].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[index].dstSet=sets[index];writes[index].descriptorCount=1;writes[index].descriptorType=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;writes[index].pImageInfo=&image_infos[index];}
  buffer_info.buffer=readback.buffer;buffer_info.range=BYTES;writes[2].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[2].dstSet=sets[1];writes[2].dstBinding=1;writes[2].descriptorCount=1;writes[2].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[2].pBufferInfo=&buffer_info;g.vkUpdateDescriptorSets(g.device,3,writes,0,NULL);
  OK(g.vkResetCommandPool(g.device,commands,0));begin.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;OK(g.vkBeginCommandBuffer(command,&begin));barrier.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;barrier.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;g.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_VERTEX_INPUT_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&barrier,0,NULL,0,NULL);
  transition(command,source.raw.image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,0,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);g.vkCmdBindVertexBuffers(command,0,1,&vertices.buffer,&offset);
  begin_image(command,b.render[s][si],source_fb,sw,sh,s);
  constants[1]=identity_ms_mask(s);constants[2]=identity_ms_alpha_mask(s);constants[3]=identity_ms_base(s);
  for(physical_sample=0;physical_sample<source_samples;++physical_sample){constants[0]=physical_sample;g.vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,b.fill_pso[format_index(formats[s])][si][physical_sample]);g.vkCmdPushConstants(command,b.fill_layout,VK_SHADER_STAGE_FRAGMENT_BIT,0,16,constants);g.vkCmdDraw(command,6,1,0,0);}
  g.vkCmdEndRenderPass(command);
  if(typed_resources) {
   transition(command,source.raw.image,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
   transition(command,typed_source.image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,VK_ACCESS_TRANSFER_WRITE_BIT);
   identity_ms_copy(copy_image,command,source.raw.image,typed_source.image,sw,sh);
   transition(command,typed_source.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
  } else transition(command,source.raw.image,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
  transition(command,dest.raw.image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,0,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
  begin_image(command,b.render[s][di],dest_fb,dw,dh,s);g.vkCmdEndRenderPass(command);
  if(typed_resources) {
   transition(command,dest.raw.image,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
   transition(command,typed_dest.image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,VK_ACCESS_TRANSFER_WRITE_BIT);
   identity_ms_copy(copy_image,command,dest.raw.image,typed_dest.image,dw,dh);
   transition(command,typed_dest.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
  } else transition(command,dest.raw.image,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
  if(dynamic_rendering)begin_dynamic_image(command,typed_resources?typed_dest.view:dest.numeric,dw,dh);else begin_image(command,b.numeric_render[s][di],numeric_fb,dw,dh,s);
  g.vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,active_layout,0,1,&sets[0],0,NULL);
  for(physical_sample=0;physical_sample<(dm && mode?samples:1);++physical_sample) {
   if(!identity_ms_active(&key,physical_sample))continue;
   packet.sample=physical_sample;logical=dm==1 && !n?physical_sample/3:physical_sample;
   if(bad_sample && dm==2 && mode)packet.sample=(physical_sample+1)&3;
   if(bad_mask && dm==1 && !n && mode)logical^=1;
   CHECK(active_pipelines[logical]);g.vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,active_pipelines[logical]);
   if(variant) {if(!physical_sample)g.vkCmdPushConstants(command,active_layout,VK_SHADER_STAGE_FRAGMENT_BIT,0,4,&packed);}
   else if(!physical_sample)g.vkCmdPushConstants(command,active_layout,VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(packet),&packet);
   else g.vkCmdPushConstants(command,active_layout,VK_SHADER_STAGE_FRAGMENT_BIT,16,4,&packet.sample);
   g.vkCmdDraw(command,6,1,0,0);
  }
  if(dynamic_rendering)im.end_rendering(command);else g.vkCmdEndRenderPass(command);
  if(typed_resources) {
   transition(command,typed_dest.image,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
   transition(command,dest.raw.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
   identity_ms_copy(copy_image,command,typed_dest.image,dest.raw.image,dw,dh);
   transition(command,dest.raw.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
  } else transition(command,dest.raw.image,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
  records=dw*dh*samples;gx=(records+63)/64+1;if(gx>65535)gx=65535;gy=((records+63)/64+1+gx-1)/gx;
  constants[0]=dw;constants[1]=dh;constants[2]=samples;constants[3]=gx*64;
  g.vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,b.probe_pso[di!=0]);g.vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,b.probe_layout,0,1,&sets[1],0,NULL);g.vkCmdPushConstants(command,b.probe_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,16,constants);g.vkCmdDispatch(command,gx,gy,1);
  barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;g.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,NULL,0,NULL);OK(g.vkEndCommandBuffer(command));OK(g.vkResetFences(g.device,1,&fence));submit.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;submit.commandBufferCount=1;submit.pCommandBuffers=&command;OK(g.vkQueueSubmit(queue,1,&submit,fence));
  if(g.vkWaitForFences(g.device,1,&fence,VK_TRUE,UINT64_C(10000000000))!=VK_SUCCESS){fprintf(stderr,"Pending MSAA GPU resources retained\n");exit(1);}CHECK(cache_host(&readback,1));pixels=(uint32_t*)readback.mapped;
  for(y=0;y<dh;++y)for(x=0;x<dw;++x)for(physical_sample=0;physical_sample<samples;++physical_sample){identity_ms_expected(&key,s,x,y,physical_sample,wanted);index=((y*dw+x)*samples+physical_sample)*4;for(lane=0;lane<4;++lane)if(pixels[index+lane]!=wanted[lane]){fprintf(stderr,"Actual RGBA %08x %08x %08x %08x; expected %08x %08x %08x %08x\n",pixels[index],pixels[index+1],pixels[index+2],pixels[index+3],wanted[0],wanted[1],wanted[2],wanted[3]);fprintf(stderr,"MS image sf%u df%u sm%u dm%u native%u scale%u mode%u pixel%u,%u sample%u lane%u %08x != %08x\n",formats[s],formats[s],sm,dm,n,c,mode,x,y,physical_sample,lane,pixels[index+lane],wanted[lane]);goto done;}}
  for(x=records*4;x<BYTES/4;++x)CHECK(pixels[x]==0x6d6d6d6du);
  if(variant==1) {
   packet.sample=0;
   probe.bank_=&b;probe.key=&key;probe.readback=&readback;probe.pool=commands;
   probe.command=command;probe.queue=queue;probe.fence=fence;probe.copy=copy_image;
   probe.typed=typed_dest.image;probe.oracle=dest.raw.image;probe.descriptor=sets[1];
   probe.oracle_fb=dest_fb;probe.view=s;probe.width=dw;probe.height=dh;probe.samples=samples;probe.bytes=BYTES;
   timed.pool=commands;timed.command=command;timed.queue=queue;timed.fence=fence;
   timed.pass=b.numeric_render[s][di];timed.framebuffer=numeric_fb;timed.vertices=vertices.buffer;
   timed.image=sets[0];timed.destination=typed_dest.image;timed.numeric_view=typed_dest.view;
   timed.width=dw;timed.height=dh;timed.view=s;timed.scale=1u<<c;
   timed.layout[0]=ready->layout;timed.layout[1]=identity_legacy.layout;timed.packet=&packet;timed.packed=packed;
   timed.source_msaa=sm;timed.source_samples=source_samples;timed.msaa=dm;timed.native=n;timed.mode=mode;timed.samples=samples;timed.active_samples=1u<<dm;
   timed.cap_native2=(unsigned)native2;timed.cap_shading=(unsigned)shading;
   timed.dynamic_rendering=(unsigned)dynamic_rendering;timed.tiles_x=IDENTITY_TILES_X;timed.tiles_y=IDENTITY_TILES_Y;
   for(logical=0;logical<(dm && mode?(1u<<dm):1u);++logical) {
    timed.pipeline[0][logical]=ready_pipelines[logical];
    timed.pipeline[1][logical]=identity_legacy.pipeline[n][s][dm][sm][c][mode][logical];
   }
   timed.prepare=identity_msaa_prepare;timed.validate=identity_msaa_probe;timed.validation_context=&probe;
   CHECK(identity_warm_profile(&timed));
  }
  g.vkDestroyFramebuffer(g.device,source_fb,NULL);if(dest_fb)g.vkDestroyFramebuffer(g.device,dest_fb,NULL);if(numeric_fb)g.vkDestroyFramebuffer(g.device,numeric_fb,NULL);source_fb=dest_fb=numeric_fb=VK_NULL_HANDLE;identity_ms_close_image(&source);identity_ms_close_image(&dest);close_image(&typed_source);close_image(&typed_dest);++cases;if(!dm)++single_cases;else if(mode)++manual_cases;else++builtin_cases;
 }
 CHECK(cases==(native2?2u:1u)*4*3*3*(shading?5u:3u)*2);
 printf("PASS: %u typed numeric MSAA parent/bank images (%u single/%u sample-shaded/%u masked), all four views/1x2x4x, native2=%d sampleShading=%d, independent physical patterns, inactive samples and probe tails\n",cases,single_cases,builtin_cases,manual_cases,native2,shading);result=0;
done:
 if(g.device) {
  if(!identity_warm_close())result=1;
  if(source_fb)g.vkDestroyFramebuffer(g.device,source_fb,NULL);
  if(dest_fb)g.vkDestroyFramebuffer(g.device,dest_fb,NULL);
  if(numeric_fb)g.vkDestroyFramebuffer(g.device,numeric_fb,NULL);
  identity_ms_close_image(&source);identity_ms_close_image(&dest);identity_legacy_close();close_bank(&b);
  close_image(&typed_source);close_image(&typed_dest);
  if(fence)g.vkDestroyFence(g.device,fence,NULL);
  if(commands)g.vkDestroyCommandPool(g.device,commands,NULL);
  if(pool)g.vkDestroyDescriptorPool(g.device,pool,NULL);
  if(image_layout)g.vkDestroyDescriptorSetLayout(g.device,image_layout,NULL);
  if(probe_layout)g.vkDestroyDescriptorSetLayout(g.device,probe_layout,NULL);
  close_host(&vertices);close_host(&readback);
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
