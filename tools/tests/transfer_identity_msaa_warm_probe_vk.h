/* Independent post-batch image verification; outside GPU timing. BSD; C89. */
typedef struct identity_msaa_probe_context {
 const bank *bank_;const xe_transfer_address_key *key;struct buffer *readback;
 VkCommandPool pool;VkCommandBuffer command;VkQueue queue;VkFence fence;
 PFN_vkCmdCopyImage copy;VkImage typed,oracle;VkDescriptorSet descriptor;
 VkFramebuffer oracle_fb;
 unsigned view,width,height,samples,bytes;
} identity_msaa_probe_context;
#define IMP_OK(x) do {if((x)!=VK_SUCCESS){fprintf(stderr,"MSAA probe API failure line%u\n",(unsigned)__LINE__);return 0;}}while(0)
static int identity_msaa_prepare(void *opaque)
{
 identity_msaa_probe_context *c=(identity_msaa_probe_context*)opaque;
 VkCommandBufferBeginInfo begin={0};VkSubmitInfo submit={0};unsigned index;
 if(!c || !c->bank_ || !c->oracle_fb)return 0;
 index=c->samples==4?2:c->samples==2?1:0;
 IMP_OK(g.vkResetCommandPool(g.device,c->pool,0));begin.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
 IMP_OK(g.vkBeginCommandBuffer(c->command,&begin));
 transition(c->command,c->oracle,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
 begin_image(c->command,c->bank_->render[c->view][index],c->oracle_fb,c->width,c->height,c->view);
 g.vkCmdEndRenderPass(c->command);
 transition(c->command,c->oracle,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
 transition(c->command,c->typed,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
  VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
 identity_ms_copy(c->copy,c->command,c->oracle,c->typed,c->width,c->height);
 transition(c->command,c->typed,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
  VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
 transition(c->command,c->oracle,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
  VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_SHADER_READ_BIT);
 IMP_OK(g.vkEndCommandBuffer(c->command));IMP_OK(g.vkResetFences(g.device,1,&c->fence));
 submit.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;submit.commandBufferCount=1;submit.pCommandBuffers=&c->command;
 if(g.vkQueueSubmit(c->queue,1,&submit,c->fence)!=VK_SUCCESS ||
  g.vkWaitForFences(g.device,1,&c->fence,VK_TRUE,UINT64_C(10000000000))!=VK_SUCCESS) {
  fprintf(stderr,"Pending MSAA sentinel preparation retained\n");exit(1);
 }
 return 1;
}
static int identity_msaa_probe(void *opaque)
{
 identity_msaa_probe_context *c=(identity_msaa_probe_context*)opaque;
 VkCommandBufferBeginInfo begin={0};VkSubmitInfo submit={0};VkMemoryBarrier barrier={0};
 uint32_t constants[4],wanted[4],*actual;unsigned x,y,s,lane,index,records,groups,gx,gy;
 if(!c || !c->bank_ || !c->key || !c->readback || !c->width || !c->height)return 0;
 records=c->width*c->height*c->samples;groups=(records+63)/64+1;
 gx=groups>65535?65535:groups;gy=(groups+gx-1)/gx;
 memset(c->readback->mapped,0x6d,c->bytes);if(!cache_host(c->readback,0))return 0;
 IMP_OK(g.vkResetCommandPool(g.device,c->pool,0));begin.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
 IMP_OK(g.vkBeginCommandBuffer(c->command,&begin));
 barrier.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;barrier.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_WRITE_BIT;
 g.vkCmdPipelineBarrier(c->command,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&barrier,0,NULL,0,NULL);
 transition(c->command,c->oracle,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
 identity_ms_copy(c->copy,c->command,c->typed,c->oracle,c->width,c->height);
 transition(c->command,c->oracle,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
  VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
 constants[0]=c->width;constants[1]=c->height;constants[2]=c->samples;constants[3]=gx*64;
 g.vkCmdBindPipeline(c->command,VK_PIPELINE_BIND_POINT_COMPUTE,c->bank_->probe_pso[c->samples!=1]);
 g.vkCmdBindDescriptorSets(c->command,VK_PIPELINE_BIND_POINT_COMPUTE,c->bank_->probe_layout,0,1,&c->descriptor,0,NULL);
 g.vkCmdPushConstants(c->command,c->bank_->probe_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,16,constants);
 g.vkCmdDispatch(c->command,gx,gy,1);
 barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
 g.vkCmdPipelineBarrier(c->command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,NULL,0,NULL);
 IMP_OK(g.vkEndCommandBuffer(c->command));IMP_OK(g.vkResetFences(g.device,1,&c->fence));
 submit.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;submit.commandBufferCount=1;submit.pCommandBuffers=&c->command;
 if(g.vkQueueSubmit(c->queue,1,&submit,c->fence)!=VK_SUCCESS ||
  g.vkWaitForFences(g.device,1,&c->fence,VK_TRUE,UINT64_C(10000000000))!=VK_SUCCESS) {
  fprintf(stderr,"Pending MSAA probe work retained\n");exit(1);
 }
 if(!cache_host(c->readback,1))return 0;
 actual=(uint32_t*)c->readback->mapped;
 for(y=0;y<c->height;++y)for(x=0;x<c->width;++x)for(s=0;s<c->samples;++s) {
  identity_ms_expected(c->key,c->view,x,y,s,wanted);index=((y*c->width+x)*c->samples+s)*4;
  for(lane=0;lane<4;++lane)if(actual[index+lane]!=wanted[lane]) {
   fprintf(stderr,"MSAA timed-batch image mismatch view%u msaa%u native%u pixel%u,%u sample%u lane%u\n",
    c->view,c->key->dest_msaa,c->key->dest_native2,x,y,s,lane);return 0;
  }
 }
 for(index=records*4;index<c->bytes/4;++index)if(actual[index]!=0x6d6d6d6du)return 0;
 return 1;
}
