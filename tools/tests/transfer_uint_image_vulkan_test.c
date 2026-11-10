/* Actual integer texture fetch -> fragment output. Untimed C89 correctness. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#define XE_VK_TEST_GRAPHICS
#define XE_VK_TEST_VERTEX_INPUT
#define XE_VK_TEST_NO_BUFFERS
#include "vulkan_test_context.h"
#include "transfer_uint_image_cases.h"

#define CHECK(x) do {if(!(x)){fprintf(stderr,"Integer image line%u\n",(unsigned)__LINE__);goto done;}}while(0)
static struct {
 PFN_vkCreateImage create;PFN_vkDestroyImage destroy;
 PFN_vkGetImageMemoryRequirements requirements;PFN_vkBindImageMemory bind;
 PFN_vkCreateImageView view;PFN_vkDestroyImageView close_view;
 PFN_vkCmdCopyBufferToImage upload;PFN_vkCmdCopyImageToBuffer readback;
 PFN_vkCmdSetViewport viewport;PFN_vkCmdSetScissor scissor;
} im;
#define IMAGE_FN(field,name) do {PFN_vkVoidFunction p=g.get(g.instance,#name);memcpy(&im.field,&p,sizeof(p));CHECK(im.field);}while(0)
typedef struct image {VkImage image;VkImageView view;VkDeviceMemory memory;} image;
static VkFormat format(unsigned f) {
 return f==5 || f==7?VK_FORMAT_R16G16B16A16_UINT:f==14?VK_FORMAT_R32_UINT:f==15?VK_FORMAT_R32G32_UINT:VK_FORMAT_R16G16_UINT;
}
static void close_image(image *v) {
 if(v->view)im.close_view(g.device,v->view,NULL);
 if(v->image)im.destroy(g.device,v->image,NULL);
 if(v->memory)g.vkFreeMemory(g.device,v->memory,NULL);
 memset(v,0,sizeof(*v));
}
static int make_image(image *v,VkFormat f,unsigned w,unsigned h,VkImageUsageFlags usage) {
 VkImageCreateInfo info={0};VkImageViewCreateInfo view={0};
 VkMemoryRequirements req;VkMemoryAllocateInfo allocation={0};unsigned t;
 info.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;info.imageType=VK_IMAGE_TYPE_2D;
 info.format=f;info.extent.width=w;info.extent.height=h;info.extent.depth=1;
 info.mipLevels=info.arrayLayers=1;info.samples=VK_SAMPLE_COUNT_1_BIT;
 info.tiling=VK_IMAGE_TILING_OPTIMAL;info.usage=usage;
 if(im.create(g.device,&info,NULL,&v->image)!=VK_SUCCESS)return 0;
 im.requirements(g.device,v->image,&req);
 for(t=0;t<g.memory.memoryTypeCount;++t)if(req.memoryTypeBits&(1u<<t))break;
 if(t==g.memory.memoryTypeCount)return 0;
 allocation.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;allocation.allocationSize=req.size;allocation.memoryTypeIndex=t;
 if(g.vkAllocateMemory(g.device,&allocation,NULL,&v->memory)!=VK_SUCCESS ||
    im.bind(g.device,v->image,v->memory,0)!=VK_SUCCESS)return 0;
 view.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;view.image=v->image;
 view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.format=f;
 view.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
 view.subresourceRange.levelCount=view.subresourceRange.layerCount=1;
 return im.view(g.device,&view,NULL,&v->view)==VK_SUCCESS;
}
static int make_host(struct buffer *v,size_t bytes) {
 VkBufferCreateInfo info={0};VkMemoryRequirements req;VkMemoryAllocateInfo allocation={0};unsigned t;
 info.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;info.size=bytes;
 info.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT|VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
 if(g.vkCreateBuffer(g.device,&info,NULL,&v->buffer)!=VK_SUCCESS)return 0;
 g.vkGetBufferMemoryRequirements(g.device,v->buffer,&req);
 for(t=0;t<g.memory.memoryTypeCount;++t)if((req.memoryTypeBits&(1u<<t)) &&
   (g.memory.memoryTypes[t].propertyFlags&VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))break;
 if(t==g.memory.memoryTypeCount)return 0;
 allocation.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;allocation.allocationSize=req.size;allocation.memoryTypeIndex=t;
 v->coherent=(g.memory.memoryTypes[t].propertyFlags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)!=0;
 return g.vkAllocateMemory(g.device,&allocation,NULL,&v->memory)==VK_SUCCESS &&
  g.vkBindBufferMemory(g.device,v->buffer,v->memory,0)==VK_SUCCESS &&
  g.vkMapMemory(g.device,v->memory,0,VK_WHOLE_SIZE,0,&v->mapped)==VK_SUCCESS;
}
static int cache_host(struct buffer *v,int invalidate) {
 VkMappedMemoryRange range={0};if(v->coherent)return 1;
 range.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;range.memory=v->memory;range.size=VK_WHOLE_SIZE;
 return (invalidate?g.vkInvalidateMappedMemoryRanges(g.device,1,&range):g.vkFlushMappedMemoryRanges(g.device,1,&range))==VK_SUCCESS;
}
static void close_host(struct buffer *v) {
 if(v->mapped)g.vkUnmapMemory(g.device,v->memory);
 if(v->buffer)g.vkDestroyBuffer(g.device,v->buffer,NULL);
 if(v->memory)g.vkFreeMemory(g.device,v->memory,NULL);
}
static void transition(VkCommandBuffer command,VkImage v,VkImageLayout old,VkImageLayout next,
 VkPipelineStageFlags src,VkPipelineStageFlags dst,VkAccessFlags read,VkAccessFlags write) {
 VkImageMemoryBarrier b={0};b.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
 b.oldLayout=old;b.newLayout=next;b.srcAccessMask=read;b.dstAccessMask=write;
 b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.image=v;
 b.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;b.subresourceRange.levelCount=b.subresourceRange.layerCount=1;
 g.vkCmdPipelineBarrier(command,src,dst,0,0,NULL,0,NULL,1,&b);
}
int main(int argc,char **argv) {
#ifdef _WIN32
 HMODULE loader=LoadLibraryA("vulkan-1.dll");FARPROC entry;
#else
 void *loader=dlopen("libvulkan.so.1",RTLD_NOW|RTLD_LOCAL),*entry;
#endif
 static const unsigned formats[]={4,5,6,7,14,15};
 static const unsigned scales[6][4]={{1,1,1,1},{2,2,2,2},{3,5,3,5},{1,1,3,5},{3,5,1,1},{1,1,1,3}};
 enum {BYTES=240*80*8};
 VkApplicationInfo app={0};VkInstanceCreateInfo instance={0};VkPhysicalDevice physical[8];
 VkPhysicalDeviceProperties properties;VkQueueFamilyProperties families[32];
 VkDeviceQueueCreateInfo queue_info={0};VkDeviceCreateInfo device_info={0};VkQueue queue;
 VkDescriptorSetLayoutBinding binding={0};VkDescriptorSetLayoutCreateInfo descriptor_info={0};VkDescriptorSetLayout descriptor=VK_NULL_HANDLE;
 VkPushConstantRange push={VK_SHADER_STAGE_FRAGMENT_BIT,0,112};VkPipelineLayoutCreateInfo layout_info={0};VkPipelineLayout layout=VK_NULL_HANDLE;
 VkDescriptorPoolSize pool_size={VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1};VkDescriptorPoolCreateInfo pool_info={0};VkDescriptorPool pool=VK_NULL_HANDLE;
 VkDescriptorSetAllocateInfo allocate_set={0};VkDescriptorSet set;VkDescriptorImageInfo image_info={0};VkWriteDescriptorSet write={0};
 VkShaderModule vertex_module=VK_NULL_HANDLE,fragment_module=VK_NULL_HANDLE;
 VkRenderPass render[4]={0};VkPipeline pipeline[4]={0};VkFramebuffer framebuffer=VK_NULL_HANDLE;
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
 xe_transfer_uint_options options={0};xe_transfer_address_key key={0};xe_transfer_uint_packet packet;
 uint32_t count,family,s,d,c,x,y,lane,wanted[4],actual;unsigned sw,sh,dw,dh,components,bytes,index,cases=0;
 float priority=1;int result=1;char path[4096];
 CHECK(loader && argc==2 && strlen(argv[1])<2048);
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
 fprintf(stderr,"INTEGER_IMAGE_DEVICE %s driver%u\n",properties.deviceName,properties.driverVersion);
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
 sprintf(path,"%s-s0-m0-t0.spv",argv[1]);CHECK(shader(path,&fragment_module));
 binding.descriptorType=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;binding.descriptorCount=1;binding.stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT;
 descriptor_info.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;descriptor_info.bindingCount=1;descriptor_info.pBindings=&binding;OK(g.vkCreateDescriptorSetLayout(g.device,&descriptor_info,NULL,&descriptor));
 layout_info.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;layout_info.setLayoutCount=1;layout_info.pSetLayouts=&descriptor;layout_info.pushConstantRangeCount=1;layout_info.pPushConstantRanges=&push;OK(g.vkCreatePipelineLayout(g.device,&layout_info,NULL,&layout));
 pool_info.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;pool_info.maxSets=1;pool_info.poolSizeCount=1;pool_info.pPoolSizes=&pool_size;OK(g.vkCreateDescriptorPool(g.device,&pool_info,NULL,&pool));
 allocate_set.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;allocate_set.descriptorPool=pool;allocate_set.descriptorSetCount=1;allocate_set.pSetLayouts=&descriptor;OK(g.vkAllocateDescriptorSets(g.device,&allocate_set,&set));
 stages[0].sType=stages[1].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;stages[0].module=vertex_module;stages[1].module=fragment_module;stages[0].pName=stages[1].pName="main";
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
 for(c=0;c<4;++c) {attachment.format=format(c==0?4:c==1?5:c==2?14:15);OK(g.vkCreateRenderPass(g.device,&render_info,NULL,&render[c]));pipeline_info.renderPass=render[c];OK(g.vkCreateGraphicsPipelines(g.device,VK_NULL_HANDLE,1,&pipeline_info,NULL,&pipeline[c]));}
 command_info.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;command_info.queueFamilyIndex=family;OK(g.vkCreateCommandPool(g.device,&command_info,NULL,&commands));
 allocate_command.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;allocate_command.commandPool=commands;allocate_command.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;allocate_command.commandBufferCount=1;OK(g.vkAllocateCommandBuffers(g.device,&allocate_command,&command));
 fence_info.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;OK(g.vkCreateFence(g.device,&fence_info,NULL,&fence));
 key.source_color=key.dest_color=1;key.source_pitch=key.dest_pitch=1;
 for(s=0;s<6;++s)for(d=0;d<6;++d)for(c=0;c<6;++c) {
  key.source_wide=formats[s]==5 || formats[s]==7 || formats[s]==15;key.dest_wide=formats[d]==5 || formats[d]==7 || formats[d]==15;
  key.dest_scale_x=scales[c][0];key.dest_scale_y=scales[c][1];key.source_scale_x=scales[c][2];key.source_scale_y=scales[c][3];
  CHECK(xe_transfer_uint_packet_prepare(&packet,&options,&key,formats[s],formats[d],0));
  sw=packet.address.word[XE_ADDR_S_TW];sh=packet.address.word[XE_ADDR_S_TH];dw=packet.address.word[XE_ADDR_D_TW];dh=packet.address.word[XE_ADDR_D_TH];
  CHECK(make_image(&source,format(formats[s]),sw,sh,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT) && make_image(&dest,format(formats[d]),dw,dh,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT));
  components=image_components(formats[s]);bytes=image_halves(formats[s])?2:4;
  for(y=0;y<sh;++y)for(x=0;x<sw;++x)for(lane=0;lane<components;++lane) {index=(y*sw+x)*components+lane;if(bytes==2)((uint16_t*)upload.mapped)[index]=(uint16_t)image_pattern(formats[s],x,y,lane);else((uint32_t*)upload.mapped)[index]=image_pattern(formats[s],x,y,lane);}
  memset(readback.mapped,0x6d,BYTES);CHECK(cache_host(&upload,0) && cache_host(&readback,0));
  index=formats[d]==5 || formats[d]==7?1:formats[d]==14?2:formats[d]==15?3:0;
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
  im.viewport(command,0,1,&viewport);im.scissor(command,0,1,&scissor);g.vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline[index]);g.vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,0,1,&set,0,NULL);g.vkCmdBindVertexBuffers(command,0,1,&vertices.buffer,&offset);g.vkCmdPushConstants(command,layout,VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(packet),&packet);g.vkCmdDraw(command,6,1,0,0);g.vkCmdEndRenderPass(command);
  transition(command,dest.image,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
  copy.imageExtent.width=dw;copy.imageExtent.height=dh;im.readback(command,dest.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,readback.buffer,1,&copy);
  barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;g.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,NULL,0,NULL);
  OK(g.vkEndCommandBuffer(command));OK(g.vkResetFences(g.device,1,&fence));submit.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;submit.commandBufferCount=1;submit.pCommandBuffers=&command;OK(g.vkQueueSubmit(queue,1,&submit,fence));
  if(g.vkWaitForFences(g.device,1,&fence,VK_TRUE,UINT64_C(5000000000))!=VK_SUCCESS){fprintf(stderr,"Pending GPU test resources retained\n");exit(1);}CHECK(cache_host(&readback,1));
  components=image_components(formats[d]);bytes=image_halves(formats[d])?2:4;
  for(y=0;y<dh;++y)for(x=0;x<dw;++x) {image_expected(&key,formats[s],formats[d],x,y,wanted);for(lane=0;lane<components;++lane) {
   actual=bytes==2?((uint16_t*)readback.mapped)[(y*dw+x)*components+lane]:((uint32_t*)readback.mapped)[(y*dw+x)*components+lane];
   if(actual!=wanted[lane]){fprintf(stderr,"Image sf%u df%u scale%u pixel%u,%u lane%u %08x != %08x\n",formats[s],formats[d],c,x,y,lane,actual,wanted[lane]);goto done;}
  }}
  for(x=dw*dh*components*bytes;x<BYTES;++x)CHECK(((unsigned char*)readback.mapped)[x]==0x6d);
  g.vkDestroyFramebuffer(g.device,framebuffer,NULL);framebuffer=VK_NULL_HANDLE;close_image(&source);close_image(&dest);++cases;
 }
 printf("PASS: %u actual integer SS raster transfers, all36 format pairs and6 scale classes, two modules/four precreated PSOs, raw encoding and tail guards\n",cases);result=0;
done:
 if(g.device) {
  if(framebuffer)g.vkDestroyFramebuffer(g.device,framebuffer,NULL);
  close_image(&source);close_image(&dest);
  if(fence)g.vkDestroyFence(g.device,fence,NULL);
  if(commands)g.vkDestroyCommandPool(g.device,commands,NULL);
  for(c=0;c<4;++c){if(pipeline[c])g.vkDestroyPipeline(g.device,pipeline[c],NULL);if(render[c])g.vkDestroyRenderPass(g.device,render[c],NULL);}
  if(pool)g.vkDestroyDescriptorPool(g.device,pool,NULL);
  if(layout)g.vkDestroyPipelineLayout(g.device,layout,NULL);
  if(descriptor)g.vkDestroyDescriptorSetLayout(g.device,descriptor,NULL);
  if(vertex_module)g.vkDestroyShaderModule(g.device,vertex_module,NULL);
  if(fragment_module)g.vkDestroyShaderModule(g.device,fragment_module,NULL);
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
