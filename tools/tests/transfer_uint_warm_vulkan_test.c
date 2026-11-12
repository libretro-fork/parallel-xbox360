/* Actual independent MS sample initialization, transfer and probe. C89 test. */
#include "transfer_uint_vulkan_image.h"
#include "transfer_msaa_image_cases.h"
#include "libretro/core_transfer_uint_vk.h"
typedef struct bank {
 VkPipelineLayout fill_layout,probe_layout;
 VkShaderModule vertex,fill,probe[2];
 VkRenderPass render[4][3];
 VkPipeline fill_pso[4][3][4],probe_pso[2];
 xe_transfer_uint_vk_bank ready[2][2][4];
} bank;
static unsigned sample_count(unsigned index){return 1u<<index;}
static unsigned format_index(unsigned f){return f==5 || f==7?1:f==14?2:f==15?3:0;}
static int load_modules(bank *b,const char *prefix) {
 char path[4096];unsigned s;
 sprintf(path,"%s-vertex.spv",prefix);if(!shader(path,&b->vertex))return 0;
 sprintf(path,"%s-fill.spv",prefix);if(!shader(path,&b->fill))return 0;
 for(s=0;s<2;++s) {
  sprintf(path,"%s-probe-s%u.spv",prefix,s);if(!shader(path,&b->probe[s]))return 0;

 }
 return 1;
}
static void bank_ops(xe_transfer_uint_vk_ops *ops) {
 ops->create_shader=g.vkCreateShaderModule;ops->destroy_shader=g.vkDestroyShaderModule;
 ops->create_layout=g.vkCreatePipelineLayout;ops->destroy_layout=g.vkDestroyPipelineLayout;
 ops->create_pipelines=g.vkCreateGraphicsPipelines;ops->destroy_pipeline=g.vkDestroyPipeline;
}
static int make_bank(bank *b,VkDescriptorSetLayout image_set,VkDescriptorSetLayout probe_set,
 int native2,int sample_shading,int dynamic_rendering) {
 VkPipelineLayoutCreateInfo layout={0};VkPushConstantRange push={VK_SHADER_STAGE_FRAGMENT_BIT,0,112};
 VkAttachmentDescription attachment={0};VkAttachmentReference reference={0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
 VkSubpassDescription subpass={0};VkRenderPassCreateInfo render={0};
 VkPipelineShaderStageCreateInfo stages[2]={{0},{0}};VkVertexInputBindingDescription binding={0};VkVertexInputAttributeDescription attribute={0};
 VkPipelineVertexInputStateCreateInfo vertex={0};VkPipelineInputAssemblyStateCreateInfo assembly={0};VkPipelineViewportStateCreateInfo viewport={0};
 VkPipelineRasterizationStateCreateInfo raster={0};VkPipelineMultisampleStateCreateInfo msaa={0};
 VkPipelineColorBlendAttachmentState blend_attachment={0};VkPipelineColorBlendStateCreateInfo blend={0};
 VkDynamicState dynamic_values[2]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};VkPipelineDynamicStateCreateInfo dynamic={0};
 VkGraphicsPipelineCreateInfo pipeline={0};VkComputePipelineCreateInfo compute={0};
 unsigned f,c,s,n,kind,scale;VkSampleMask mask=1;
 xe_spv_binary scratch={0};xe_transfer_uint_vk_config config={0};xe_transfer_uint_vk_ops ops;
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
  attachment.format=format(f==0?4:f==1?5:f==2?14:15);attachment.samples=(VkSampleCountFlagBits)sample_count(c);msaa.rasterizationSamples=attachment.samples;
  if(g.vkCreateRenderPass(g.device,&render,NULL,&b->render[f][c])!=VK_SUCCESS)return 0;
  pipeline.renderPass=b->render[f][c];pipeline.layout=b->fill_layout;stages[1].module=b->fill;msaa.sampleShadingEnable=VK_FALSE;
  for(s=0;s<sample_count(c);++s) {mask=1u<<s;if(g.vkCreateGraphicsPipelines(g.device,VK_NULL_HANDLE,1,&pipeline,NULL,&b->fill_pso[f][c][s])!=VK_SUCCESS)return 0;}
 }
 compute.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;compute.layout=b->probe_layout;compute.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;compute.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;compute.stage.pName="main";
 for(s=0;s<2;++s){compute.stage.module=b->probe[s];if(g.vkCreateComputePipelines(g.device,VK_NULL_HANDLE,1,&compute,NULL,&b->probe_pso[s])!=VK_SUCCESS)return 0;}
 bank_ops(&ops);config.device=g.device;config.image_layout=image_set;config.vertex=b->vertex;config.dynamic_rendering=(unsigned)dynamic_rendering;
 for(n=1;n<2;++n)for(kind=0;kind<1;++kind)for(scale=0;scale<3;++scale) {
  if(kind==0 && !sample_shading)continue;
  config.native2=n;config.sample_shading=kind==0;
  config.scale_x=config.scale_y=1u<<scale;
  for(f=0;f<4;++f)for(c=0;c<3;++c)config.passes[f][c]=b->render[f][c==1 && !n?2:c];
  XE_COMPILE_BEGIN();
  if(!xe_transfer_uint_vk_init(&b->ready[n][kind][scale],&config,&ops,&scratch)){xe_spv_binary_close(&scratch);return 0;}
  XE_COMPILE_END("vulkan.uint_bank.init",(n*2+kind)*4+scale);
 }
 xe_spv_binary_close(&scratch);
 return 1;
}
static void close_bank(bank *b) {
 unsigned f,c,s,n,kind,scale;xe_transfer_uint_vk_ops ops;bank_ops(&ops);
 for(n=0;n<2;++n)for(kind=0;kind<2;++kind)for(scale=0;scale<4;++scale)xe_transfer_uint_vk_close(&b->ready[n][kind][scale],&ops);
 for(f=0;f<4;++f)for(c=0;c<3;++c) {
  for(s=0;s<4;++s)if(b->fill_pso[f][c][s])g.vkDestroyPipeline(g.device,b->fill_pso[f][c][s],NULL);

  if(b->render[f][c])g.vkDestroyRenderPass(g.device,b->render[f][c],NULL);
 }
 for(s=0;s<2;++s){if(b->probe_pso[s])g.vkDestroyPipeline(g.device,b->probe_pso[s],NULL);if(b->probe[s])g.vkDestroyShaderModule(g.device,b->probe[s],NULL);}
 if(b->vertex)g.vkDestroyShaderModule(g.device,b->vertex,NULL);
 if(b->fill)g.vkDestroyShaderModule(g.device,b->fill,NULL);
 if(b->fill_layout)g.vkDestroyPipelineLayout(g.device,b->fill_layout,NULL);
 if(b->probe_layout)g.vkDestroyPipelineLayout(g.device,b->probe_layout,NULL);
}
static VkResult framebuffer(VkRenderPass render,image *v,unsigned w,unsigned h,VkFramebuffer *out) {
 VkFramebufferCreateInfo info={0};info.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;info.renderPass=render;info.attachmentCount=1;info.pAttachments=&v->view;info.width=w;info.height=h;info.layers=1;
 return g.vkCreateFramebuffer(g.device,&info,NULL,out);
}
static void begin_image(VkCommandBuffer command,VkRenderPass render,VkFramebuffer framebuffer_,unsigned w,unsigned h) {
 VkRenderPassBeginInfo begin={0};VkClearValue clear={0};VkViewport viewport={0};VkRect2D scissor={{0,0},{0,0}};
 clear.color.uint32[0]=clear.color.uint32[1]=clear.color.uint32[2]=clear.color.uint32[3]=7;
 begin.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;begin.renderPass=render;begin.framebuffer=framebuffer_;begin.renderArea.extent.width=w;begin.renderArea.extent.height=h;begin.clearValueCount=1;begin.pClearValues=&clear;
 g.vkCmdBeginRenderPass(command,&begin,VK_SUBPASS_CONTENTS_INLINE);viewport.width=(float)w;viewport.height=(float)h;viewport.maxDepth=1;scissor.extent.width=w;scissor.extent.height=h;im.viewport(command,0,1,&viewport);im.scissor(command,0,1,&scissor);
}
static void begin_dynamic_image(VkCommandBuffer command,VkImageView view,unsigned w,unsigned h) {
 VkRenderingInfo info={0};VkRenderingAttachmentInfo color={0};VkViewport viewport={0};VkRect2D scissor={{0,0},{0,0}};
 color.sType=VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;color.imageView=view;color.imageLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;color.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;color.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
 color.clearValue.color.uint32[0]=color.clearValue.color.uint32[1]=color.clearValue.color.uint32[2]=color.clearValue.color.uint32[3]=7;
 info.sType=VK_STRUCTURE_TYPE_RENDERING_INFO;info.renderArea.extent.width=w;info.renderArea.extent.height=h;info.layerCount=1;info.colorAttachmentCount=1;info.pColorAttachments=&color;
 im.begin_rendering(command,&info);viewport.width=(float)w;viewport.height=(float)h;viewport.maxDepth=1;scissor.extent.width=w;scissor.extent.height=h;im.viewport(command,0,1,&viewport);im.scissor(command,0,1,&scissor);
}
#include "transfer_uint_warm_profile.h"
int main(int argc,char **argv) {
#ifdef _WIN32
 HMODULE loader=LoadLibraryA("vulkan-1.dll");FARPROC entry;
#else
 void *loader=dlopen("libvulkan.so.1",RTLD_NOW|RTLD_LOCAL),*entry;
#endif
 static const unsigned formats[]={4,5,6,7,14,15};
 static const unsigned scales[3][4]={{1,1,1,1},{2,2,2,2},{4,4,4,4}};
 enum {BYTES=256*1024*1024+256};
 VkApplicationInfo app={0};VkInstanceCreateInfo instance={0};VkPhysicalDevice physical[8];VkPhysicalDeviceProperties properties;VkPhysicalDeviceFeatures features={0},enabled={0};VkImageFormatProperties image_properties;
 VkPhysicalDeviceDynamicRenderingFeatures dynamic_features={0};VkPhysicalDeviceFeatures2 features2={0};
 VkQueueFamilyProperties families[32];VkDeviceQueueCreateInfo queue_info={0};VkDeviceCreateInfo device_info={0};VkQueue queue;float priority=1;
 VkDescriptorSetLayoutBinding bindings[2]={{0},{0}};VkDescriptorSetLayoutCreateInfo set_info={0};VkDescriptorSetLayout image_layout=VK_NULL_HANDLE,probe_layout=VK_NULL_HANDLE;
 VkDescriptorPoolSize sizes[2]={{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,2},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1}};VkDescriptorPoolCreateInfo pool_info={0};VkDescriptorPool pool=VK_NULL_HANDLE;
 VkDescriptorSetLayout layouts[2];VkDescriptorSet sets[2];VkDescriptorSetAllocateInfo allocate_sets={0};VkDescriptorImageInfo image_infos[2]={{0},{0}};VkDescriptorBufferInfo buffer_info={0};VkWriteDescriptorSet writes[3]={{0},{0},{0}};
 VkCommandPoolCreateInfo command_info={0};VkCommandPool commands=VK_NULL_HANDLE;VkCommandBufferAllocateInfo allocate_command={0};VkCommandBuffer command;
 VkFenceCreateInfo fence_info={0};VkFence fence=VK_NULL_HANDLE;VkCommandBufferBeginInfo begin={0};VkSubmitInfo submit={0};VkMemoryBarrier barrier={0};VkDeviceSize offset=0;
 bank b={0};image source={0},dest={0};VkFramebuffer source_fb=VK_NULL_HANDLE,dest_fb=VK_NULL_HANDLE;struct buffer readback={0},vertices={0};
 xe_transfer_address_key key={0};xe_transfer_uint_options options={0};xe_transfer_uint_packet packet;
 uint32_t count,family,sample_flags=UINT32_MAX,constants[4],wanted[4],*pixels;
 unsigned f,s,d,dm,sm,n,c,mode,si,di,samples,source_samples,sw,sh,dw,dh,x,y,lane,physical_sample,index,logical,records,cases=0,manual_cases=0,builtin_cases=0,single_cases=0;
 int native2=0,shading=0,result=1,dynamic_rendering=getenv("MSAA_DYNAMIC_RENDERING")!=NULL;
 const xe_transfer_uint_vk_bank *ready;const VkPipeline *ready_pipelines;
 size_t active_bytes;int pair_index,reference=getenv("TRANSFER_REFERENCE")!=NULL;warm_case profiled;uint32_t legacy_constant;
 CHECK(loader && argc==2 && strlen(argv[1])<2048);
#ifdef _WIN32
 entry=GetProcAddress(loader,"vkGetInstanceProcAddr");
#else
 entry=dlsym(loader,"vkGetInstanceProcAddr");
#endif
 memcpy(&g.get,&entry,sizeof(g.get));CHECK(g.get);LOAD(vkCreateInstance);
 app.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO;app.apiVersion=dynamic_rendering?VK_API_VERSION_1_3:VK_API_VERSION_1_0;instance.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;instance.pApplicationInfo=&app;OK(g.vkCreateInstance(&instance,NULL,&g.instance));
 LOAD(vkDestroyInstance);LOAD(vkEnumeratePhysicalDevices);LOAD(vkGetPhysicalDeviceProperties);LOAD(vkGetPhysicalDeviceMemoryProperties);LOAD(vkGetPhysicalDeviceQueueFamilyProperties);LOAD(vkCreateDevice);IMAGE_FN(features,vkGetPhysicalDeviceFeatures);IMAGE_FN(properties,vkGetPhysicalDeviceImageFormatProperties);
 count=8;OK(g.vkEnumeratePhysicalDevices(g.instance,&count,physical));CHECK(count);g.vkGetPhysicalDeviceProperties(physical[0],&properties);g.vkGetPhysicalDeviceMemoryProperties(physical[0],&g.memory);im.features(physical[0],&features);
 CHECK(properties.deviceType==VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU || properties.deviceType==VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU);
 CHECK(features.pipelineStatisticsQuery);enabled.pipelineStatisticsQuery=VK_TRUE;
 enabled.sampleRateShading=features.sampleRateShading && !getenv("MSAA_DISABLE_SAMPLE_SHADING");shading=enabled.sampleRateShading!=0;
 if(dynamic_rendering) {
  CHECK(properties.apiVersion>=VK_API_VERSION_1_3);LOAD(vkGetPhysicalDeviceFeatures2);
  dynamic_features.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;features2.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;features2.pNext=&dynamic_features;g.vkGetPhysicalDeviceFeatures2(physical[0],&features2);CHECK(dynamic_features.dynamicRendering);device_info.pNext=&dynamic_features;
 }
 for(f=0;f<4;++f){OK(im.properties(physical[0],format(f==0?4:f==1?5:f==2?14:15),VK_IMAGE_TYPE_2D,VK_IMAGE_TILING_OPTIMAL,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,0,&image_properties));sample_flags&=image_properties.sampleCounts;}
 CHECK((sample_flags&5)==5);native2=(sample_flags&2)!=0;
 CHECK(!dynamic_rendering && native2 && shading);
 fprintf(stderr,"TRANSFER_WARM_DEVICE %s vendor%u device%u driver%u api%u native2=%d sampleShading=%d dynamicRendering=%d\n",properties.deviceName,properties.vendorID,properties.deviceID,properties.driverVersion,properties.apiVersion,native2,shading,dynamic_rendering);
 fprintf(stderr,"TRANSFER_WARM_LIMITS storage%u image%u\n",properties.limits.maxStorageBufferRange,properties.limits.maxImageDimension2D);
 count=32;g.vkGetPhysicalDeviceQueueFamilyProperties(physical[0],&count,families);for(family=0;family<count;++family)if((families[family].queueFlags&(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT))==(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT))break;CHECK(family<count);
 queue_info.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;queue_info.queueFamilyIndex=family;queue_info.queueCount=1;queue_info.pQueuePriorities=&priority;device_info.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;device_info.queueCreateInfoCount=1;device_info.pQueueCreateInfos=&queue_info;device_info.pEnabledFeatures=&enabled;OK(g.vkCreateDevice(physical[0],&device_info,NULL,&g.device));
 LOAD(vkDestroyDevice);LOAD(vkGetDeviceQueue);LOAD(vkCreateBuffer);LOAD(vkDestroyBuffer);LOAD(vkGetBufferMemoryRequirements);LOAD(vkAllocateMemory);LOAD(vkFreeMemory);LOAD(vkBindBufferMemory);LOAD(vkMapMemory);LOAD(vkUnmapMemory);LOAD(vkFlushMappedMemoryRanges);LOAD(vkInvalidateMappedMemoryRanges);LOAD(vkCreateShaderModule);LOAD(vkDestroyShaderModule);LOAD(vkCreateDescriptorSetLayout);LOAD(vkDestroyDescriptorSetLayout);LOAD(vkCreatePipelineLayout);LOAD(vkDestroyPipelineLayout);LOAD(vkCreateGraphicsPipelines);LOAD(vkCreateComputePipelines);LOAD(vkDestroyPipeline);LOAD(vkCreateRenderPass);LOAD(vkDestroyRenderPass);LOAD(vkCreateFramebuffer);LOAD(vkDestroyFramebuffer);
 LOAD(vkCreateDescriptorPool);LOAD(vkDestroyDescriptorPool);LOAD(vkAllocateDescriptorSets);LOAD(vkUpdateDescriptorSets);LOAD(vkCreateCommandPool);LOAD(vkDestroyCommandPool);LOAD(vkAllocateCommandBuffers);LOAD(vkResetCommandPool);LOAD(vkBeginCommandBuffer);LOAD(vkEndCommandBuffer);LOAD(vkCmdPipelineBarrier);LOAD(vkCmdBindPipeline);LOAD(vkCmdBindDescriptorSets);LOAD(vkCmdPushConstants);LOAD(vkCmdBindVertexBuffers);LOAD(vkCmdBeginRenderPass);LOAD(vkCmdEndRenderPass);LOAD(vkCmdDraw);LOAD(vkCmdDispatch);LOAD(vkCreateFence);LOAD(vkDestroyFence);LOAD(vkResetFences);LOAD(vkQueueSubmit);LOAD(vkWaitForFences);
 IMAGE_FN(create,vkCreateImage);IMAGE_FN(destroy,vkDestroyImage);IMAGE_FN(requirements,vkGetImageMemoryRequirements);IMAGE_FN(bind,vkBindImageMemory);IMAGE_FN(view,vkCreateImageView);IMAGE_FN(close_view,vkDestroyImageView);IMAGE_FN(viewport,vkCmdSetViewport);IMAGE_FN(scissor,vkCmdSetScissor);
 if(dynamic_rendering){IMAGE_FN(begin_rendering,vkCmdBeginRendering);IMAGE_FN(end_rendering,vkCmdEndRendering);}
 g.vkGetDeviceQueue(g.device,family,0,&queue);CHECK(make_host(&readback,BYTES) && make_host(&vertices,16));{float *v=(float*)vertices.mapped;v[0]=v[1]=-1;v[2]=v[3]=1;}CHECK(cache_host(&vertices,0));
 bindings[0].descriptorType=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;bindings[0].descriptorCount=1;bindings[0].stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT;set_info.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;set_info.bindingCount=1;set_info.pBindings=bindings;OK(g.vkCreateDescriptorSetLayout(g.device,&set_info,NULL,&image_layout));
 bindings[0].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;bindings[1].binding=1;bindings[1].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;bindings[1].descriptorCount=1;bindings[1].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;set_info.bindingCount=2;OK(g.vkCreateDescriptorSetLayout(g.device,&set_info,NULL,&probe_layout));
 CHECK(load_modules(&b,argv[1]) && make_bank(&b,image_layout,probe_layout,native2,shading,dynamic_rendering));
 pool_info.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;pool_info.maxSets=2;pool_info.poolSizeCount=2;pool_info.pPoolSizes=sizes;OK(g.vkCreateDescriptorPool(g.device,&pool_info,NULL,&pool));layouts[0]=image_layout;layouts[1]=probe_layout;allocate_sets.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;allocate_sets.descriptorPool=pool;allocate_sets.descriptorSetCount=2;allocate_sets.pSetLayouts=layouts;OK(g.vkAllocateDescriptorSets(g.device,&allocate_sets,sets));
 command_info.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;command_info.queueFamilyIndex=family;OK(g.vkCreateCommandPool(g.device,&command_info,NULL,&commands));allocate_command.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;allocate_command.commandPool=commands;allocate_command.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;allocate_command.commandBufferCount=1;OK(g.vkAllocateCommandBuffers(g.device,&allocate_command,&command));fence_info.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;OK(g.vkCreateFence(g.device,&fence_info,NULL,&fence));
 CHECK(getenv("TRANSFER_GPU_CSV") || getenv("TRANSFER_REFERENCE") || getenv("TRANSFER_CORRECTNESS_ONLY"));
 CHECK(warm_init(&b,image_layout,commands,families[family].timestampValidBits,properties.limits.timestampPeriod));
 key.source_color=key.dest_color=1;key.source_pitch=33;key.dest_pitch=32;key.dest_to_source_base=1;
 for(n=0;n<2;++n)for(s=0;s<6;++s)for(d=0;d<6;++d)for(sm=0;sm<3;++sm)for(dm=0;dm<3;++dm)for(c=0;c<3;++c)for(mode=0;mode<2;++mode) {
  pair_index=warm_pair(formats[s],formats[d]);if(n!=1 || mode || dm!=sm || pair_index<0)continue;
  key.source_msaa=sm;key.dest_msaa=dm;key.source_native2=key.dest_native2=n;key.source_wide=formats[s]==5 || formats[s]==7 || formats[s]==15;key.dest_wide=formats[d]==5 || formats[d]==7 || formats[d]==15;
  key.dest_scale_x=scales[c][0];key.dest_scale_y=scales[c][1];key.source_scale_x=scales[c][2];key.source_scale_y=scales[c][3];options.source_multisampled=sm!=0;options.sample_mode=dm?(mode?XE_TRANSFER_UINT_SAMPLE_CONSTANT:XE_TRANSFER_UINT_SAMPLE_BUILTIN):XE_TRANSFER_UINT_SINGLE_SAMPLE;
  CHECK(xe_transfer_uint_packet_prepare(&packet,&options,&key,formats[s],formats[d],0));
  CHECK(warm_reference_packet(&key,formats[s],formats[d]));
  ready=&b.ready[n][mode || !shading?1:0][c==0?0:c==1?1:c==5?3:2];ready_pipelines=xe_transfer_uint_vk_get(ready,formats[d],dm,sm,c==3 || c==5,c==4);CHECK(ready_pipelines);sw=33*packet.address.word[XE_ADDR_S_TW];sh=16*packet.address.word[XE_ADDR_S_TH];dw=32*packet.address.word[XE_ADDR_D_TW];dh=16*packet.address.word[XE_ADDR_D_TH];
  si=sm==0?0:sm==1 && n?1:2;di=dm==0?0:dm==1 && n?1:2;source_samples=sample_count(si);samples=sample_count(di);records=dw*dh*samples;active_bytes=(size_t)records*16+256;CHECK(active_bytes<=BYTES && active_bytes<=properties.limits.maxStorageBufferRange);
  CHECK(make_image_samples(&source,format(formats[s]),sw,sh,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,source_samples) && make_image_samples(&dest,format(formats[d]),dw,dh,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,samples));
  OK(framebuffer(b.render[format_index(formats[s])][si],&source,sw,sh,&source_fb));if(!dynamic_rendering)OK(framebuffer(b.render[format_index(formats[d])][di],&dest,dw,dh,&dest_fb));
  memset(readback.mapped,0x6d,active_bytes);CHECK(cache_host(&readback,0));
  image_infos[0].imageView=source.view;image_infos[0].imageLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;image_infos[1].imageView=dest.view;image_infos[1].imageLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  for(index=0;index<2;++index){writes[index].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[index].dstSet=sets[index];writes[index].descriptorCount=1;writes[index].descriptorType=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;writes[index].pImageInfo=&image_infos[index];}
  buffer_info.buffer=readback.buffer;buffer_info.range=active_bytes;writes[2].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[2].dstSet=sets[1];writes[2].dstBinding=1;writes[2].descriptorCount=1;writes[2].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[2].pBufferInfo=&buffer_info;g.vkUpdateDescriptorSets(g.device,3,writes,0,NULL);
  OK(g.vkResetCommandPool(g.device,commands,0));begin.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;OK(g.vkBeginCommandBuffer(command,&begin));barrier.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;barrier.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;g.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_VERTEX_INPUT_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&barrier,0,NULL,0,NULL);
  transition(command,source.image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,0,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);g.vkCmdBindVertexBuffers(command,0,1,&vertices.buffer,&offset);
  begin_image(command,b.render[format_index(formats[s])][si],source_fb,sw,sh);
  constants[1]=image_halves(formats[s])?65535:UINT32_MAX;constants[2]=constants[3]=0;
  for(physical_sample=0;physical_sample<source_samples;++physical_sample){constants[0]=physical_sample;g.vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,b.fill_pso[format_index(formats[s])][si][physical_sample]);g.vkCmdPushConstants(command,b.fill_layout,VK_SHADER_STAGE_FRAGMENT_BIT,0,16,constants);g.vkCmdDraw(command,6,1,0,0);}
  g.vkCmdEndRenderPass(command);transition(command,source.image,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
  transition(command,dest.image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,0,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);if(dynamic_rendering)begin_dynamic_image(command,dest.view,dw,dh);else begin_image(command,b.render[format_index(formats[d])][di],dest_fb,dw,dh);g.vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,reference?warm_reference_layout(c):ready->layout,0,1,&sets[0],0,NULL);
  for(physical_sample=0;physical_sample<(dm && mode?samples:1);++physical_sample) {
   if(!image_ms_active(&key,physical_sample))continue;
   packet.sample=physical_sample;logical=dm==1 && !n?physical_sample/3:physical_sample;
   g.vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,reference?warm.legacy[pair_index][dm][c]:ready_pipelines[logical]);
   legacy_constant=key.dest_pitch|(key.source_pitch<<10)|(((uint32_t)key.dest_to_source_base&4095)<<20);
   if(reference)g.vkCmdPushConstants(command,warm_reference_layout(c),VK_SHADER_STAGE_FRAGMENT_BIT,0,
    warm.revision.api?XE_TEST_TRANSFER_PACKET_BYTES:4,warm.revision.api?warm.parent_packet:&legacy_constant);
   else if(!physical_sample)g.vkCmdPushConstants(command,ready->layout,VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(packet),&packet);
   else g.vkCmdPushConstants(command,ready->layout,VK_SHADER_STAGE_FRAGMENT_BIT,108,4,&packet.sample);
   g.vkCmdDraw(command,6,1,0,0);
  }
  if(dynamic_rendering)im.end_rendering(command);else g.vkCmdEndRenderPass(command);transition(command,dest.image,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
  constants[0]=dw;constants[1]=dh;constants[2]=samples;constants[3]=65536;records=dw*dh*samples;
  g.vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,b.probe_pso[di!=0]);g.vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,b.probe_layout,0,1,&sets[1],0,NULL);g.vkCmdPushConstants(command,b.probe_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,16,constants);g.vkCmdDispatch(command,1024,(records+65535)/65536+1,1);
  barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;g.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,NULL,0,NULL);OK(g.vkEndCommandBuffer(command));OK(g.vkResetFences(g.device,1,&fence));submit.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;submit.commandBufferCount=1;submit.pCommandBuffers=&command;OK(g.vkQueueSubmit(queue,1,&submit,fence));
  if(g.vkWaitForFences(g.device,1,&fence,VK_TRUE,UINT64_C(10000000000))!=VK_SUCCESS){fprintf(stderr,"Pending MSAA GPU resources retained\n");exit(1);}CHECK(cache_host(&readback,1));pixels=(uint32_t*)readback.mapped;
  for(y=0;y<dh;++y)for(x=0;x<dw;++x)for(physical_sample=0;physical_sample<samples;++physical_sample){image_ms_expected(&key,formats[s],formats[d],x,y,physical_sample,wanted);index=((y*dw+x)*samples+physical_sample)*4;for(lane=0;lane<image_components(formats[d]);++lane)if(pixels[index+lane]!=wanted[lane]){fprintf(stderr,"Actual RGBA %08x %08x %08x %08x; expected %08x %08x %08x %08x\n",pixels[index],pixels[index+1],pixels[index+2],pixels[index+3],wanted[0],wanted[1],wanted[2],wanted[3]);fprintf(stderr,"MS image sf%u df%u sm%u dm%u native%u scale%u mode%u pixel%u,%u sample%u lane%u %08x != %08x\n",formats[s],formats[d],sm,dm,n,c,mode,x,y,physical_sample,lane,pixels[index+lane],wanted[lane]);goto done;}}
  for(x=records*4;x<active_bytes/4;++x)CHECK(pixels[x]==0x6d6d6d6du);
  memset(&profiled,0,sizeof(profiled));profiled.bank_=&b;profiled.ready=ready;profiled.packet=&packet;profiled.key=&key;profiled.pool=commands;profiled.command=command;profiled.queue=queue;profiled.fence=fence;profiled.vertices=vertices.buffer;profiled.descriptor=sets[0];profiled.framebuffer=dest_fb;profiled.destination=&dest;profiled.generic=ready_pipelines[0];profiled.width=dw;profiled.height=dh;profiled.pair=(unsigned)pair_index;profiled.msaa=dm;profiled.scale=c;CHECK(warm_profile(&profiled));
  g.vkDestroyFramebuffer(g.device,source_fb,NULL);if(dest_fb)g.vkDestroyFramebuffer(g.device,dest_fb,NULL);source_fb=dest_fb=VK_NULL_HANDLE;close_image(&source);close_image(&dest);++cases;if(!dm)++single_cases;else if(mode)++manual_cases;else++builtin_cases;
 }
 CHECK(cases==90 && single_cases==30 && builtin_cases==60 && !manual_cases);printf("PASS: %u exact production-bank images, reference=%d, 1x2x4x/native2 and raw format crossings\n",cases,reference);result=0;
done:
 if(g.device) {
  if(source_fb)g.vkDestroyFramebuffer(g.device,source_fb,NULL);
  if(dest_fb)g.vkDestroyFramebuffer(g.device,dest_fb,NULL);
  close_image(&source);close_image(&dest);warm_close();close_bank(&b);
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
