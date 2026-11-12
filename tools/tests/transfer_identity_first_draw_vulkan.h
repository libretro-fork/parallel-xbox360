/* First prepared transfer, normal typed images. Test-only; BSD; MSVC C89.
 * Upload/clear completes before recording; readback lies outside GPU timing. */
#include "gpu_timestamp_measure.h"
typedef struct identity_first_draw {
 double record_seconds,submit_complete_seconds,validation_seconds,gpu_seconds;
 uint64_t primitives,vertices,fragments;unsigned pixels;
} identity_first_draw;
static uint32_t first_pattern(unsigned x,unsigned y) {
 return ((x*13+y*7)&255u)|(((x^(y*11))&255u)<<8)|(((x*3+y*5)&255u)<<16)|UINT32_C(0xa5000000);
}
static int identity_first_transfer(const xe_transfer_identity_vk_bank *ready,
 const xe_transfer_identity_vk_config *config,unsigned family,unsigned timestamp_bits,
 float timestamp_period,identity_first_draw *measurement) {
 VkQueue queue;VkCommandPool commands=VK_NULL_HANDLE;VkCommandBuffer command;
 VkCommandPoolCreateInfo command_info={0};VkCommandBufferAllocateInfo allocate={0};
 VkCommandBufferBeginInfo begin={0};VkSubmitInfo submit={0};VkFence fence=VK_NULL_HANDLE;VkFenceCreateInfo fence_info={0};
 VkDescriptorPool pool=VK_NULL_HANDLE;VkDescriptorPoolSize pool_size={VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1};
 VkDescriptorPoolCreateInfo pool_info={0};VkDescriptorSetAllocateInfo set_info={0};VkDescriptorSet set;
 VkDescriptorImageInfo image_info={0};VkWriteDescriptorSet write={0};
 VkFramebuffer framebuffer=VK_NULL_HANDLE;VkFramebufferCreateInfo framebuffer_info={0};VkRenderPassBeginInfo render={0};
 VkRenderingAttachmentInfo attachment={0};VkRenderingInfo rendering={0};VkClearColorValue clear={{0}};
 VkImageSubresourceRange subresource={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};VkBufferImageCopy copy={0};
 VkMemoryBarrier barrier={0};VkViewport viewport={0};VkRect2D scissor={{0,0},{0,0}};
 VkQueryPool timestamps=VK_NULL_HANDLE,statistics=VK_NULL_HANDLE;VkQueryPoolCreateInfo query={0};
 PFN_vkCmdBeginQuery begin_query;PFN_vkCmdEndQuery end_query;PFN_vkCmdClearColorImage clear_image;PFN_vkVoidFunction entry;
 struct buffer upload={0},readback={0};image source={0},dest={0};
 xe_transfer_address_key key={0};xe_transfer_identity_options options={0};xe_transfer_identity_packet packet;
 const VkPipeline *pipeline;VkDeviceSize offset;uint64_t ticks[4]={0},stats[4]={0};
 unsigned w=80*config->scale_x,h=16*config->scale_y,x,y,i,bytes=w*h*4;double before;int result=0,pending=0;
 LOAD(vkGetDeviceQueue);LOAD(vkCreateBuffer);LOAD(vkDestroyBuffer);LOAD(vkGetBufferMemoryRequirements);
 LOAD(vkAllocateMemory);LOAD(vkFreeMemory);LOAD(vkBindBufferMemory);LOAD(vkMapMemory);LOAD(vkUnmapMemory);
 LOAD(vkFlushMappedMemoryRanges);LOAD(vkInvalidateMappedMemoryRanges);
 IMAGE_FN(create,vkCreateImage);IMAGE_FN(destroy,vkDestroyImage);IMAGE_FN(requirements,vkGetImageMemoryRequirements);
 IMAGE_FN(bind,vkBindImageMemory);IMAGE_FN(view,vkCreateImageView);IMAGE_FN(close_view,vkDestroyImageView);
 IMAGE_FN(upload,vkCmdCopyBufferToImage);IMAGE_FN(readback,vkCmdCopyImageToBuffer);IMAGE_FN(viewport,vkCmdSetViewport);IMAGE_FN(scissor,vkCmdSetScissor);
 LOAD(vkCreateFramebuffer);LOAD(vkDestroyFramebuffer);LOAD(vkCreateDescriptorPool);LOAD(vkDestroyDescriptorPool);
 LOAD(vkAllocateDescriptorSets);LOAD(vkUpdateDescriptorSets);LOAD(vkCreateCommandPool);LOAD(vkDestroyCommandPool);
 LOAD(vkAllocateCommandBuffers);LOAD(vkResetCommandPool);LOAD(vkBeginCommandBuffer);LOAD(vkEndCommandBuffer);
 LOAD(vkCmdPipelineBarrier);LOAD(vkCmdBindPipeline);LOAD(vkCmdBindDescriptorSets);LOAD(vkCmdPushConstants);LOAD(vkCmdBindVertexBuffers);
 LOAD(vkCmdBeginRenderPass);LOAD(vkCmdEndRenderPass);LOAD(vkCmdDraw);LOAD(vkCreateFence);LOAD(vkDestroyFence);
 LOAD(vkResetFences);LOAD(vkQueueSubmit);LOAD(vkWaitForFences);
 LOAD(vkCreateQueryPool);LOAD(vkDestroyQueryPool);LOAD(vkGetQueryPoolResults);LOAD(vkCmdResetQueryPool);LOAD(vkCmdWriteTimestamp);
 entry=g.get(g.instance,"vkCmdBeginQuery");memcpy(&begin_query,&entry,sizeof(entry));CHECK(begin_query);
 entry=g.get(g.instance,"vkCmdEndQuery");memcpy(&end_query,&entry,sizeof(entry));CHECK(end_query);
 entry=g.get(g.instance,"vkCmdClearColorImage");memcpy(&clear_image,&entry,sizeof(entry));CHECK(clear_image);
 if(config->dynamic_rendering){IMAGE_FN(begin_rendering,vkCmdBeginRendering);IMAGE_FN(end_rendering,vkCmdEndRendering);}
 CHECK(timestamp_bits>=36 && timestamp_period>0);
 CHECK(make_host(&upload,bytes+16) && make_host(&readback,bytes+256));
 for(y=0;y<h;++y)for(x=0;x<w;++x)((uint32_t*)upload.mapped)[y*w+x]=first_pattern(x,y);
 {float *v=(float*)((unsigned char*)upload.mapped+bytes);v[0]=v[1]=-1;v[2]=v[3]=1;}
 memset(readback.mapped,0x6d,bytes+256);CHECK(cache_host(&upload,0) && cache_host(&readback,0));
 CHECK(make_image(&source,VK_FORMAT_R8G8B8A8_UNORM,w,h,VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT));
 CHECK(make_image(&dest,VK_FORMAT_R8G8B8A8_UNORM,w,h,VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT));
 pool_info.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;pool_info.maxSets=1;pool_info.poolSizeCount=1;pool_info.pPoolSizes=&pool_size;
 OK(g.vkCreateDescriptorPool(g.device,&pool_info,NULL,&pool));set_info.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
 set_info.descriptorPool=pool;set_info.descriptorSetCount=1;set_info.pSetLayouts=&config->image_layout;OK(g.vkAllocateDescriptorSets(g.device,&set_info,&set));
 image_info.imageView=source.view;image_info.imageLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
 write.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;write.dstSet=set;write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;write.pImageInfo=&image_info;
 g.vkUpdateDescriptorSets(g.device,1,&write,0,NULL);
 if(!config->dynamic_rendering){framebuffer_info.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;framebuffer_info.renderPass=config->passes[0][0];
  framebuffer_info.attachmentCount=1;framebuffer_info.pAttachments=&dest.view;framebuffer_info.width=w;framebuffer_info.height=h;framebuffer_info.layers=1;
  OK(g.vkCreateFramebuffer(g.device,&framebuffer_info,NULL,&framebuffer));}
 query.sType=VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;query.queryType=VK_QUERY_TYPE_TIMESTAMP;query.queryCount=2;OK(g.vkCreateQueryPool(g.device,&query,NULL,&timestamps));
 query.queryType=VK_QUERY_TYPE_PIPELINE_STATISTICS;query.queryCount=1;query.pipelineStatistics=VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_PRIMITIVES_BIT|VK_QUERY_PIPELINE_STATISTIC_VERTEX_SHADER_INVOCATIONS_BIT|VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT;
 OK(g.vkCreateQueryPool(g.device,&query,NULL,&statistics));
 command_info.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;command_info.queueFamilyIndex=family;OK(g.vkCreateCommandPool(g.device,&command_info,NULL,&commands));
 allocate.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;allocate.commandPool=commands;allocate.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;allocate.commandBufferCount=1;OK(g.vkAllocateCommandBuffers(g.device,&allocate,&command));
 fence_info.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;OK(g.vkCreateFence(g.device,&fence_info,NULL,&fence));g.vkGetDeviceQueue(g.device,family,0,&queue);
 begin.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;OK(g.vkBeginCommandBuffer(command,&begin));
 barrier.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;barrier.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
 g.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,0,1,&barrier,0,NULL,0,NULL);
 transition(command,source.image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,VK_ACCESS_TRANSFER_WRITE_BIT);
 copy.imageSubresource.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;copy.imageSubresource.layerCount=1;copy.imageExtent.width=w;copy.imageExtent.height=h;copy.imageExtent.depth=1;
 im.upload(command,upload.buffer,source.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
 transition(command,source.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
 transition(command,dest.image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,VK_ACCESS_TRANSFER_WRITE_BIT);
 clear_image(command,dest.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&clear,1,&subresource);
 transition(command,dest.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
 OK(g.vkEndCommandBuffer(command));submit.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;submit.commandBufferCount=1;submit.pCommandBuffers=&command;
 OK(g.vkQueueSubmit(queue,1,&submit,fence));pending=1;
 if(g.vkWaitForFences(g.device,1,&fence,VK_TRUE,UINT64_C(10000000000))!=VK_SUCCESS)goto done;
 pending=0;OK(g.vkResetFences(g.device,1,&fence));
 key.source_color=key.dest_color=1;key.source_pitch=key.dest_pitch=1;key.source_native2=key.dest_native2=config->native2;
 key.source_scale_x=key.dest_scale_x=config->scale_x;key.source_scale_y=key.dest_scale_y=config->scale_y;
 CHECK(xe_transfer_identity_packet_prepare(&packet,&options,&key,0,0,0,0));pipeline=xe_transfer_identity_vk_get(ready,0,0,0,0,0);CHECK(pipeline);
 viewport.width=(float)w;viewport.height=(float)h;viewport.maxDepth=1;scissor.extent.width=w;scissor.extent.height=h;offset=bytes;
 before=init_now();OK(g.vkResetCommandPool(g.device,commands,0));OK(g.vkBeginCommandBuffer(command,&begin));
 g.vkCmdResetQueryPool(command,timestamps,0,2);g.vkCmdResetQueryPool(command,statistics,0,1);g.vkCmdWriteTimestamp(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,timestamps,0);
 begin_query(command,statistics,0,0);
 if(config->dynamic_rendering){attachment.sType=VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;attachment.imageView=dest.view;attachment.imageLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  attachment.loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
  rendering.sType=VK_STRUCTURE_TYPE_RENDERING_INFO;rendering.renderArea=scissor;rendering.layerCount=1;rendering.colorAttachmentCount=1;rendering.pColorAttachments=&attachment;
  im.begin_rendering(command,&rendering);
 }else{render.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;render.renderPass=config->passes[0][0];render.framebuffer=framebuffer;render.renderArea=scissor;g.vkCmdBeginRenderPass(command,&render,VK_SUBPASS_CONTENTS_INLINE);}
 im.viewport(command,0,1,&viewport);im.scissor(command,0,1,&scissor);g.vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,*pipeline);
 g.vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,ready->layout,0,1,&set,0,NULL);g.vkCmdPushConstants(command,ready->layout,VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(packet),&packet);
 g.vkCmdBindVertexBuffers(command,0,1,&upload.buffer,&offset);if(!getenv("IDENTITY_FIRST_SKIP_DRAW"))g.vkCmdDraw(command,6,1,0,0);
 if(config->dynamic_rendering)im.end_rendering(command);else g.vkCmdEndRenderPass(command);
 end_query(command,statistics,0);g.vkCmdWriteTimestamp(command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,timestamps,1);
 transition(command,dest.image,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
 im.readback(command,dest.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,readback.buffer,1,&copy);
 barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;g.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,NULL,0,NULL);
 OK(g.vkEndCommandBuffer(command));measurement->record_seconds=init_now()-before;
 before=init_now();OK(g.vkQueueSubmit(queue,1,&submit,fence));pending=1;
 if(g.vkWaitForFences(g.device,1,&fence,VK_TRUE,UINT64_C(10000000000))!=VK_SUCCESS)goto done;
 pending=0;measurement->submit_complete_seconds=init_now()-before;before=init_now();CHECK(cache_host(&readback,1));
 for(y=0;y<h;++y)for(x=0;x<w;++x)if(((const uint32_t*)readback.mapped)[y*w+x]!=first_pattern(x,y)){fprintf(stderr,"First transfer image mismatch %u,%u\n",x,y);goto done;}
 for(i=bytes;i<bytes+256;++i)CHECK(((const unsigned char*)readback.mapped)[i]==0x6d);
 OK(g.vkGetQueryPoolResults(g.device,timestamps,0,2,sizeof(ticks),ticks,16,VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT));
 OK(g.vkGetQueryPoolResults(g.device,statistics,0,1,sizeof(stats),stats,sizeof(stats),VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT));
 CHECK(stats[3] && stats[0]==2 && stats[1] && stats[2]>=(uint64_t)w*h);
 CHECK(xe_test_gpu_timestamp_seconds(ticks,timestamp_bits,timestamp_period,measurement->submit_complete_seconds,&measurement->gpu_seconds));
 measurement->primitives=stats[0];measurement->vertices=stats[1];measurement->fragments=stats[2];measurement->pixels=w*h;
 measurement->validation_seconds=init_now()-before;result=1;
done:
 if(pending){fprintf(stderr,"Pending first-transfer resources retained\n");exit(1);}
 if(statistics)g.vkDestroyQueryPool(g.device,statistics,NULL);
 if(timestamps)g.vkDestroyQueryPool(g.device,timestamps,NULL);
 if(fence)g.vkDestroyFence(g.device,fence,NULL);
 if(commands)g.vkDestroyCommandPool(g.device,commands,NULL);
 if(framebuffer)g.vkDestroyFramebuffer(g.device,framebuffer,NULL);
 if(pool)g.vkDestroyDescriptorPool(g.device,pool,NULL);
 close_image(&dest);close_image(&source);close_host(&readback);close_host(&upload);return result;
}
