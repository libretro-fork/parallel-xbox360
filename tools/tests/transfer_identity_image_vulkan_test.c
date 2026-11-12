/* Numeric-view identity raster, exhaustive UNORM16 codes, independent routes. BSD; C89. */
#include "transfer_uint_vulkan_image.h"
#include "transfer_identity_image_cases.h"
#include "libretro/core_transfer_identity_vk.h"
static VkFormat identity_format(unsigned view) {
 static const VkFormat views[4]={VK_FORMAT_R8G8B8A8_UNORM,VK_FORMAT_R16G16B16A16_UNORM,VK_FORMAT_A2B10G10R10_UNORM_PACK32,VK_FORMAT_R16G16B16A16_SFLOAT};
 return views[view];
}
int main(int argc,char **argv) {
#ifdef _WIN32
 HMODULE loader=LoadLibraryA("vulkan-1.dll");FARPROC entry;
#else
 void *loader=dlopen("libvulkan.so.1",RTLD_NOW|RTLD_LOCAL),*entry;
#endif
 static const unsigned formats[]={0,1,2,3};
 static const unsigned scales[4][4]={{2,3,2,3},{2,3,1,1},{1,1,2,3},{1,1,1,1}};
 enum {BYTES=4194304};
 VkApplicationInfo app={0};VkInstanceCreateInfo instance={0};VkPhysicalDevice physical[8];
 VkPhysicalDeviceProperties properties;VkQueueFamilyProperties families[32];
 VkDeviceQueueCreateInfo queue_info={0};VkDeviceCreateInfo device_info={0};VkQueue queue;
 VkDescriptorSetLayoutBinding binding={0};VkDescriptorSetLayoutCreateInfo descriptor_info={0};VkDescriptorSetLayout descriptor=VK_NULL_HANDLE;
 VkPushConstantRange push={VK_SHADER_STAGE_FRAGMENT_BIT,0,20};VkPipelineLayoutCreateInfo layout_info={0};VkPipelineLayout layout=VK_NULL_HANDLE;
 VkDescriptorPoolSize pool_size={VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1};VkDescriptorPoolCreateInfo pool_info={0};VkDescriptorPool pool=VK_NULL_HANDLE;
 VkDescriptorSetAllocateInfo allocate_set={0};VkDescriptorSet set;VkDescriptorImageInfo image_info={0};VkWriteDescriptorSet write={0};
 VkShaderModule vertex_module=VK_NULL_HANDLE,fragment_module[4]={0};
 VkRenderPass render[4]={0};VkPipeline pipeline[4][4]={{0}};VkFramebuffer framebuffer=VK_NULL_HANDLE;
 VkPipeline legacy_pipeline[4][4]={{0}};VkPipelineLayout legacy_layout=VK_NULL_HANDLE;
 VkShaderModule legacy_module=VK_NULL_HANDLE;const char *legacy_prefix=getenv("IDENTITY_LEGACY_PREFIX");
 uint32_t legacy_packet;
 VkPipelineShaderStageCreateInfo stages[2]={{0},{0}};VkVertexInputBindingDescription vb={0};VkVertexInputAttributeDescription va={0};
 VkPipelineVertexInputStateCreateInfo vertex={0};VkPipelineInputAssemblyStateCreateInfo assembly={0};
 VkPipelineViewportStateCreateInfo viewport_info={0};VkPipelineRasterizationStateCreateInfo raster={0};
 VkPipelineMultisampleStateCreateInfo msaa={0};VkPipelineColorBlendAttachmentState blend_attachment={0};VkPipelineColorBlendStateCreateInfo blend={0};
 VkDynamicState dynamics[2]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};VkPipelineDynamicStateCreateInfo dynamic={0};
 VkGraphicsPipelineCreateInfo pipeline_info={0};VkAttachmentDescription attachment={0};VkAttachmentReference reference={0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
 VkSubpassDescription subpass={0};VkRenderPassCreateInfo render_info={0};VkFramebufferCreateInfo framebuffer_info={0};
 VkCommandPoolCreateInfo command_info={0};VkCommandPool commands=VK_NULL_HANDLE;VkCommandBufferAllocateInfo allocate_command={0};VkCommandBuffer command;
 VkFenceCreateInfo fence_info={0};VkFence fence=VK_NULL_HANDLE;VkCommandBufferBeginInfo begin={0};VkSubmitInfo submit={0};
 VkRenderPassBeginInfo render_begin={0};VkViewport viewport={0};VkRect2D scissor={{0,0},{0,0}};
 VkBufferImageCopy copy={0};VkMemoryBarrier barrier={0};VkDeviceSize offset=0;
 struct buffer upload={0},readback={0},vertices={0};image source={0},dest={0};
 xe_transfer_identity_options options={0};xe_transfer_address_key key={0};xe_transfer_identity_packet packet;
 xe_transfer_identity_vk_bank bank={0};xe_transfer_identity_vk_config bank_config={0};
 xe_transfer_identity_vk_ops bank_ops={0};xe_spv_binary bank_scratch={0};
 VkPipelineLayout active_layout;VkPipeline active_pipeline;const VkPipeline *prepared;
 uint32_t count,family,s,c,x,y,lane,wanted[4],actual;unsigned sw,sh,dw,dh,components,bytes,index,cases=0,variant;
 float priority=1;int result=1;char path[4096];
 CHECK(loader && argc==2 && strlen(argv[1])<2048);
 CHECK(!legacy_prefix || strlen(legacy_prefix)<2048);
#ifdef _WIN32
 entry=GetProcAddress(loader,"vkGetInstanceProcAddr");
#else
 entry=dlsym(loader,"vkGetInstanceProcAddr");
#endif
 memcpy(&g.get,&entry,sizeof(g.get));CHECK(g.get);LOAD(vkCreateInstance);
 app.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO;app.apiVersion=VK_API_VERSION_1_0;
 instance.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;instance.pApplicationInfo=&app;OK(g.vkCreateInstance(&instance,NULL,&g.instance));
 LOAD(vkDestroyInstance);LOAD(vkEnumeratePhysicalDevices);LOAD(vkGetPhysicalDeviceProperties);LOAD(vkGetPhysicalDeviceMemoryProperties);LOAD(vkGetPhysicalDeviceQueueFamilyProperties);LOAD(vkCreateDevice);
 count=8;OK(g.vkEnumeratePhysicalDevices(g.instance,&count,physical));CHECK(count);
 g.vkGetPhysicalDeviceProperties(physical[0],&properties);g.vkGetPhysicalDeviceMemoryProperties(physical[0],&g.memory);
 fprintf(stderr,"IDENTITY_IMAGE_DEVICE %s driver%u\n",properties.deviceName,properties.driverVersion);
 count=32;g.vkGetPhysicalDeviceQueueFamilyProperties(physical[0],&count,families);
 for(family=0;family<count;++family)if(families[family].queueFlags&VK_QUEUE_GRAPHICS_BIT)break;
 CHECK(family<count);queue_info.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;queue_info.queueFamilyIndex=family;queue_info.queueCount=1;queue_info.pQueuePriorities=&priority;
 device_info.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;device_info.queueCreateInfoCount=1;device_info.pQueueCreateInfos=&queue_info;OK(g.vkCreateDevice(physical[0],&device_info,NULL,&g.device));
 LOAD(vkDestroyDevice);LOAD(vkGetDeviceQueue);LOAD(vkCreateBuffer);LOAD(vkDestroyBuffer);LOAD(vkGetBufferMemoryRequirements);LOAD(vkAllocateMemory);LOAD(vkFreeMemory);LOAD(vkBindBufferMemory);LOAD(vkMapMemory);LOAD(vkUnmapMemory);LOAD(vkFlushMappedMemoryRanges);LOAD(vkInvalidateMappedMemoryRanges);
 LOAD(vkCreateShaderModule);LOAD(vkDestroyShaderModule);LOAD(vkCreateDescriptorSetLayout);LOAD(vkDestroyDescriptorSetLayout);LOAD(vkCreatePipelineLayout);LOAD(vkDestroyPipelineLayout);LOAD(vkCreateGraphicsPipelines);LOAD(vkDestroyPipeline);LOAD(vkCreateRenderPass);LOAD(vkDestroyRenderPass);LOAD(vkCreateFramebuffer);LOAD(vkDestroyFramebuffer);
 LOAD(vkCreateDescriptorPool);LOAD(vkDestroyDescriptorPool);LOAD(vkAllocateDescriptorSets);LOAD(vkUpdateDescriptorSets);LOAD(vkCreateCommandPool);LOAD(vkDestroyCommandPool);LOAD(vkAllocateCommandBuffers);LOAD(vkResetCommandPool);LOAD(vkBeginCommandBuffer);LOAD(vkEndCommandBuffer);LOAD(vkCmdPipelineBarrier);LOAD(vkCmdBindPipeline);LOAD(vkCmdBindDescriptorSets);LOAD(vkCmdPushConstants);LOAD(vkCmdBindVertexBuffers);LOAD(vkCmdBeginRenderPass);LOAD(vkCmdEndRenderPass);LOAD(vkCmdDraw);LOAD(vkCreateFence);LOAD(vkDestroyFence);LOAD(vkResetFences);LOAD(vkQueueSubmit);LOAD(vkWaitForFences);
 IMAGE_FN(create,vkCreateImage);IMAGE_FN(destroy,vkDestroyImage);IMAGE_FN(requirements,vkGetImageMemoryRequirements);IMAGE_FN(bind,vkBindImageMemory);IMAGE_FN(view,vkCreateImageView);IMAGE_FN(close_view,vkDestroyImageView);IMAGE_FN(upload,vkCmdCopyBufferToImage);IMAGE_FN(readback,vkCmdCopyImageToBuffer);IMAGE_FN(viewport,vkCmdSetViewport);IMAGE_FN(scissor,vkCmdSetScissor);
 g.vkGetDeviceQueue(g.device,family,0,&queue);
 CHECK(make_host(&upload,BYTES) && make_host(&readback,BYTES) && make_host(&vertices,16));
 {float *v=(float*)vertices.mapped;v[0]=v[1]=-1;v[2]=v[3]=1;}CHECK(cache_host(&vertices,0));
 sprintf(path,"%s-vertex.spv",argv[1]);CHECK(shader(path,&vertex_module));
 for(c=0;c<4;++c){sprintf(path,"%s-n1-d0-s0-c%u-t0-m0.spv",argv[1],c);CHECK(shader(path,&fragment_module[c]));}
 binding.descriptorType=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;binding.descriptorCount=1;binding.stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT;
 descriptor_info.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;descriptor_info.bindingCount=1;descriptor_info.pBindings=&binding;OK(g.vkCreateDescriptorSetLayout(g.device,&descriptor_info,NULL,&descriptor));
 layout_info.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;layout_info.setLayoutCount=1;layout_info.pSetLayouts=&descriptor;layout_info.pushConstantRangeCount=1;layout_info.pPushConstantRanges=&push;OK(g.vkCreatePipelineLayout(g.device,&layout_info,NULL,&layout));
 pool_info.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;pool_info.maxSets=1;pool_info.poolSizeCount=1;pool_info.pPoolSizes=&pool_size;OK(g.vkCreateDescriptorPool(g.device,&pool_info,NULL,&pool));
 allocate_set.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;allocate_set.descriptorPool=pool;allocate_set.descriptorSetCount=1;allocate_set.pSetLayouts=&descriptor;OK(g.vkAllocateDescriptorSets(g.device,&allocate_set,&set));
 stages[0].sType=stages[1].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;stages[0].module=vertex_module;stages[1].module=fragment_module[0];stages[0].pName=stages[1].pName="main";
 vb.stride=16;vb.inputRate=VK_VERTEX_INPUT_RATE_INSTANCE;va.format=VK_FORMAT_R32G32B32A32_SFLOAT;
 vertex.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;vertex.vertexBindingDescriptionCount=vertex.vertexAttributeDescriptionCount=1;vertex.pVertexBindingDescriptions=&vb;vertex.pVertexAttributeDescriptions=&va;
 assembly.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
 viewport_info.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;viewport_info.viewportCount=viewport_info.scissorCount=1;
 raster.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;raster.polygonMode=VK_POLYGON_MODE_FILL;raster.lineWidth=1;
 msaa.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;msaa.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
 blend_attachment.colorWriteMask=15;blend.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;blend.attachmentCount=1;blend.pAttachments=&blend_attachment;
 dynamic.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;dynamic.dynamicStateCount=2;dynamic.pDynamicStates=dynamics;
 pipeline_info.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;pipeline_info.stageCount=2;pipeline_info.pStages=stages;pipeline_info.pVertexInputState=&vertex;pipeline_info.pInputAssemblyState=&assembly;pipeline_info.pViewportState=&viewport_info;pipeline_info.pRasterizationState=&raster;pipeline_info.pMultisampleState=&msaa;pipeline_info.pColorBlendState=&blend;pipeline_info.pDynamicState=&dynamic;pipeline_info.layout=layout;
 attachment.samples=VK_SAMPLE_COUNT_1_BIT;attachment.loadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;attachment.initialLayout=attachment.finalLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
 subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;subpass.colorAttachmentCount=1;subpass.pColorAttachments=&reference;
 render_info.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;render_info.attachmentCount=1;render_info.pAttachments=&attachment;render_info.subpassCount=1;render_info.pSubpasses=&subpass;
 for(s=0;s<4;++s) {attachment.format=identity_format(s);OK(g.vkCreateRenderPass(g.device,&render_info,NULL,&render[s]));pipeline_info.renderPass=render[s];for(c=0;c<4;++c){stages[1].module=fragment_module[c];OK(g.vkCreateGraphicsPipelines(g.device,VK_NULL_HANDLE,1,&pipeline_info,NULL,&pipeline[s][c]));}}
 if(legacy_prefix) {
  push.size=4;OK(g.vkCreatePipelineLayout(g.device,&layout_info,NULL,&legacy_layout));
  pipeline_info.layout=legacy_layout;
  for(s=0;s<4;++s)for(c=0;c<4;++c) {
   sprintf(path,"%s-ss-v%u-c%u.spv",legacy_prefix,s,c);CHECK(shader(path,&legacy_module));
   stages[1].module=legacy_module;pipeline_info.renderPass=render[s];
   OK(g.vkCreateGraphicsPipelines(g.device,VK_NULL_HANDLE,1,&pipeline_info,NULL,&legacy_pipeline[s][c]));
   g.vkDestroyShaderModule(g.device,legacy_module,NULL);legacy_module=VK_NULL_HANDLE;
  }
 }
 bank_config.device=g.device;bank_config.image_layout=descriptor;bank_config.vertex=vertex_module;
 bank_config.native2=1;bank_config.scale_x=2;bank_config.scale_y=3;
 for(s=0;s<4;++s){bank_config.sample_mask[s]=1;bank_config.passes[s][0]=render[s];}
 bank_ops.create_shader=g.vkCreateShaderModule;bank_ops.destroy_shader=g.vkDestroyShaderModule;
 bank_ops.create_layout=g.vkCreatePipelineLayout;bank_ops.destroy_layout=g.vkDestroyPipelineLayout;
 bank_ops.create_pipelines=g.vkCreateGraphicsPipelines;bank_ops.destroy_pipeline=g.vkDestroyPipeline;
 CHECK(xe_transfer_identity_vk_init(&bank,&bank_config,&bank_ops,&bank_scratch));
 command_info.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;command_info.queueFamilyIndex=family;OK(g.vkCreateCommandPool(g.device,&command_info,NULL,&commands));
 allocate_command.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;allocate_command.commandPool=commands;allocate_command.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;allocate_command.commandBufferCount=1;OK(g.vkAllocateCommandBuffers(g.device,&allocate_command,&command));
 fence_info.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;OK(g.vkCreateFence(g.device,&fence_info,NULL,&fence));
 key.source_color=key.dest_color=1;key.source_pitch=key.dest_pitch=4;key.source_native2=key.dest_native2=1;
 for(variant=0;variant<(legacy_prefix?3u:2u);++variant)for(s=0;s<4;++s)for(c=0;c<4;++c) {
  prepared=xe_transfer_identity_vk_get(&bank,s,0,0,c>>1,c&1);CHECK(prepared);
  active_pipeline=variant?*prepared:pipeline[s][c];active_layout=variant?bank.layout:layout;
  if(variant==2){active_pipeline=legacy_pipeline[s][c];active_layout=legacy_layout;}
  options.view=s;
  key.dest_scale_x=scales[c][0];key.dest_scale_y=scales[c][1];key.source_scale_x=scales[c][2];key.source_scale_y=scales[c][3];
  CHECK(xe_transfer_identity_packet_prepare(&packet,&options,&key,formats[s],formats[s],s==1,0));
  sw=320*key.source_scale_x;sh=208*key.source_scale_y;dw=320*key.dest_scale_x;dh=208*key.dest_scale_y;
  CHECK(make_image(&source,identity_format(s),sw,sh,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT) && make_image(&dest,identity_format(s),dw,dh,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT));
  components=identity_components(s);bytes=identity_bytes(s);
  for(y=0;y<sh;++y)for(x=0;x<sw;++x)for(lane=0;lane<components;++lane) {index=(y*sw+x)*components+lane;if(bytes==1)((unsigned char*)upload.mapped)[index]=(unsigned char)identity_pattern(s,x,y,lane);else if(bytes==2)((uint16_t*)upload.mapped)[index]=(uint16_t)identity_pattern(s,x,y,lane);else((uint32_t*)upload.mapped)[index]=identity_pattern(s,x,y,lane);}
  memset(readback.mapped,0x6d,BYTES);CHECK(cache_host(&upload,0) && cache_host(&readback,0));
  index=s;
  framebuffer_info.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;framebuffer_info.renderPass=render[index];framebuffer_info.attachmentCount=1;framebuffer_info.pAttachments=&dest.view;framebuffer_info.width=dw;framebuffer_info.height=dh;framebuffer_info.layers=1;OK(g.vkCreateFramebuffer(g.device,&framebuffer_info,NULL,&framebuffer));
  image_info.imageView=source.view;image_info.imageLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  write.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;write.dstSet=set;write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;write.pImageInfo=&image_info;g.vkUpdateDescriptorSets(g.device,1,&write,0,NULL);
  OK(g.vkResetCommandPool(g.device,commands,0));begin.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;OK(g.vkBeginCommandBuffer(command,&begin));
  barrier.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;barrier.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
  g.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,0,1,&barrier,0,NULL,0,NULL);
  transition(command,source.image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,VK_ACCESS_TRANSFER_WRITE_BIT);
  copy.imageSubresource.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;copy.imageSubresource.layerCount=1;copy.imageExtent.width=sw;copy.imageExtent.height=sh;copy.imageExtent.depth=1;
  im.upload(command,upload.buffer,source.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
  transition(command,source.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
  transition(command,dest.image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,0,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
  render_begin.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;render_begin.renderPass=render[index];render_begin.framebuffer=framebuffer;render_begin.renderArea.extent.width=dw;render_begin.renderArea.extent.height=dh;g.vkCmdBeginRenderPass(command,&render_begin,VK_SUBPASS_CONTENTS_INLINE);
  viewport.width=(float)dw;viewport.height=(float)dh;viewport.maxDepth=1;scissor.extent.width=dw;scissor.extent.height=dh;
  im.viewport(command,0,1,&viewport);im.scissor(command,0,1,&scissor);g.vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,active_pipeline);g.vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,active_layout,0,1,&set,0,NULL);g.vkCmdBindVertexBuffers(command,0,1,&vertices.buffer,&offset);
  if(variant==2) {
   legacy_packet=key.dest_pitch|(key.source_pitch<<10)|(((uint32_t)key.dest_to_source_base&4095)<<20);
   g.vkCmdPushConstants(command,active_layout,VK_SHADER_STAGE_FRAGMENT_BIT,0,4,&legacy_packet);
  } else g.vkCmdPushConstants(command,active_layout,VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(packet),&packet);
  g.vkCmdDraw(command,6,1,0,0);g.vkCmdEndRenderPass(command);
  transition(command,dest.image,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
  copy.imageExtent.width=dw;copy.imageExtent.height=dh;im.readback(command,dest.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,readback.buffer,1,&copy);
  barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;g.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,NULL,0,NULL);
  OK(g.vkEndCommandBuffer(command));OK(g.vkResetFences(g.device,1,&fence));submit.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;submit.commandBufferCount=1;submit.pCommandBuffers=&command;OK(g.vkQueueSubmit(queue,1,&submit,fence));
  if(g.vkWaitForFences(g.device,1,&fence,VK_TRUE,UINT64_C(5000000000))!=VK_SUCCESS){fprintf(stderr,"Pending GPU test resources retained\n");exit(1);}CHECK(cache_host(&readback,1));
  components=identity_components(s);bytes=identity_bytes(s);
  for(y=0;y<dh;++y)for(x=0;x<dw;++x) {identity_expected(&key,s,x,y,wanted);for(lane=0;lane<components;++lane) {
   actual=bytes==1?((unsigned char*)readback.mapped)[(y*dw+x)*components+lane]:bytes==2?((uint16_t*)readback.mapped)[(y*dw+x)*components+lane]:((uint32_t*)readback.mapped)[(y*dw+x)*components+lane];
   if(actual!=wanted[lane]){fprintf(stderr,"Image sf%u df%u scale%u pixel%u,%u lane%u %08x != %08x\n",formats[s],formats[s],c,x,y,lane,actual,wanted[lane]);goto done;}
  }}
  for(x=dw*dh*components*bytes;x<BYTES;++x)CHECK(((unsigned char*)readback.mapped)[x]==0x6d);
  g.vkDestroyFramebuffer(g.device,framebuffer,NULL);framebuffer=VK_NULL_HANDLE;close_image(&source);close_image(&dest);++cases;
 }
 printf("PASS: %u numeric identity SS raster cases, standalone and actual prepared bank, all65536 UNORM16 codes, RGBA8/RGB10A2/finite-half encodings, four native/asymmetric scales and tail guards\n",cases);result=0;
 if(legacy_prefix)printf("PASS: existing production numeric emitter images match the same independent legacy address oracle\n");
done:
 if(g.device) {
  if(!xe_transfer_identity_vk_close(&bank,&bank_ops))exit(1);
  xe_spv_binary_close(&bank_scratch);
  if(framebuffer)g.vkDestroyFramebuffer(g.device,framebuffer,NULL);
  close_image(&source);close_image(&dest);
  if(fence)g.vkDestroyFence(g.device,fence,NULL);
  if(commands)g.vkDestroyCommandPool(g.device,commands,NULL);
  for(s=0;s<4;++s){for(c=0;c<4;++c)if(pipeline[s][c])g.vkDestroyPipeline(g.device,pipeline[s][c],NULL);if(render[s])g.vkDestroyRenderPass(g.device,render[s],NULL);}
  if(pool)g.vkDestroyDescriptorPool(g.device,pool,NULL);
  if(layout)g.vkDestroyPipelineLayout(g.device,layout,NULL);
  for(s=0;s<4;++s)for(c=0;c<4;++c)if(legacy_pipeline[s][c])g.vkDestroyPipeline(g.device,legacy_pipeline[s][c],NULL);
  if(legacy_module)g.vkDestroyShaderModule(g.device,legacy_module,NULL);
  if(legacy_layout)g.vkDestroyPipelineLayout(g.device,legacy_layout,NULL);
  if(descriptor)g.vkDestroyDescriptorSetLayout(g.device,descriptor,NULL);
  if(vertex_module)g.vkDestroyShaderModule(g.device,vertex_module,NULL);
  for(c=0;c<4;++c)if(fragment_module[c])g.vkDestroyShaderModule(g.device,fragment_module[c],NULL);
  close_host(&vertices);close_host(&upload);close_host(&readback);
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
