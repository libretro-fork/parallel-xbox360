/* Test-only matched typed MSAA GPU work; BSD; MSVC C89.
 * Timestamp semaphores enclose submitted work. Readback/image checks are
 * outside the timed region. No integer aliases or release instrumentation. */
#include "gpu_timestamp_measure.h"
#ifndef XE_SHADER_COMPILE_BENCH
#error Build the standalone measurement tool with XE_SHADER_COMPILE_BENCH
#endif
static struct {
 PFN_vkCreateQueryPool create;PFN_vkDestroyQueryPool destroy;
 PFN_vkGetQueryPoolResults results;PFN_vkCmdResetQueryPool reset;
 PFN_vkCmdWriteTimestamp stamp;PFN_vkCmdBeginQuery begin;PFN_vkCmdEndQuery end;
 PFN_vkCreateSemaphore create_semaphore;PFN_vkDestroySemaphore destroy_semaphore;
 VkQueryPool query,stats;VkSemaphore gates[2];VkCommandBuffer marks[2];
 unsigned bits;float period;FILE *csv;
} iw;
#define IW_LOAD(field,name) do {PFN_vkVoidFunction fn=g.get(g.instance,#name);memcpy(&iw.field,&fn,sizeof(fn));if(!iw.field)return 0;}while(0)
static int identity_warm_init(VkCommandPool pool,unsigned bits,float period)
{
 VkQueryPoolCreateInfo query={0};VkSemaphoreCreateInfo semaphore={0};
 VkCommandBufferAllocateInfo commands={0};const char *csv=getenv("IDENTITY_GPU_CSV");unsigned i;
 if(!csv || bits<36 || bits>64 || !(period>0))return 0;
 iw.bits=bits;iw.period=period;
 IW_LOAD(create,vkCreateQueryPool);IW_LOAD(destroy,vkDestroyQueryPool);
 IW_LOAD(results,vkGetQueryPoolResults);IW_LOAD(reset,vkCmdResetQueryPool);
 IW_LOAD(stamp,vkCmdWriteTimestamp);IW_LOAD(begin,vkCmdBeginQuery);IW_LOAD(end,vkCmdEndQuery);
 IW_LOAD(create_semaphore,vkCreateSemaphore);IW_LOAD(destroy_semaphore,vkDestroySemaphore);
 query.sType=VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;query.queryType=VK_QUERY_TYPE_TIMESTAMP;query.queryCount=2;
 if(iw.create(g.device,&query,NULL,&iw.query)!=VK_SUCCESS)return 0;
 query.queryType=VK_QUERY_TYPE_PIPELINE_STATISTICS;query.queryCount=1;
 query.pipelineStatistics=VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_PRIMITIVES_BIT|
  VK_QUERY_PIPELINE_STATISTIC_VERTEX_SHADER_INVOCATIONS_BIT|VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT;
 if(iw.create(g.device,&query,NULL,&iw.stats)!=VK_SUCCESS)return 0;
 semaphore.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
 for(i=0;i<2;++i)if(iw.create_semaphore(g.device,&semaphore,NULL,&iw.gates[i])!=VK_SUCCESS)return 0;
 commands.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;commands.commandPool=pool;
 commands.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;commands.commandBufferCount=2;
 if(g.vkAllocateCommandBuffers(g.device,&commands,iw.marks)!=VK_SUCCESS)return 0;
 iw.csv=fopen(csv,"wb");if(!iw.csv)return 0;
 return fprintf(iw.csv,"view,scale,pixels,repeat,trial,order,reference,start,available_start,end,available_end,bits,period_ns,host_seconds,gpu_seconds,ia_primitives,vs_invocations,fs_invocations,available_stats,work_multiplier,msaa,native,mode,samples,active_samples,cap_native2,cap_shading,dynamic_rendering,tiles_x,tiles_y,width,height,source_msaa,source_samples\n")>0;
}
static int identity_warm_close(void)
{
 unsigned i;int result=1;
 if(iw.csv){result=fclose(iw.csv)==0;iw.csv=NULL;}
 if(iw.query)iw.destroy(g.device,iw.query,NULL);
 if(iw.stats)iw.destroy(g.device,iw.stats,NULL);
 for(i=0;i<2;++i)if(iw.gates[i])iw.destroy_semaphore(g.device,iw.gates[i],NULL);
 memset(&iw,0,sizeof(iw));return result;
}
typedef struct identity_warm_case {
 VkCommandPool pool;VkCommandBuffer command;VkQueue queue;VkFence fence;
 VkRenderPass pass;VkFramebuffer framebuffer;VkBuffer vertices;VkDescriptorSet image;
 VkImage destination;unsigned width,height,view,scale;
 VkPipeline pipeline[2][4];VkPipelineLayout layout[2];VkImageView numeric_view;
 unsigned source_msaa,source_samples,msaa,native,mode,samples,active_samples,cap_native2,cap_shading,dynamic_rendering,tiles_x,tiles_y;
 int (*prepare)(void*);int (*validate)(void*);void *validation_context;
 const xe_transfer_identity_packet *packet;uint32_t packed;
} identity_warm_case;
#define IW_OK(x) do {if((x)!=VK_SUCCESS){fprintf(stderr,"Numeric GPU API failure line%u\n",(unsigned)__LINE__);return 0;}}while(0)
static int identity_warm_profile(const identity_warm_case *c)
{
 VkCommandBufferBeginInfo begin={0};VkSubmitInfo submits[3]={{0},{0},{0}};
 VkPipelineStageFlags wait_stage=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;VkDeviceSize offset=0;
 VkRenderPassBeginInfo render={0};VkViewport viewport={0};VkRect2D scissor={{0,0},{0,0}};
 unsigned trial,order,reference,work,repeated,draw,multiplier,passes,logical,physical;uint64_t ticks[4],stats[4];
 double started,host,seconds;
 int reverse=getenv("IDENTITY_REVERSE_ORDER")!=NULL;
 int extra=getenv("IDENTITY_EXTRA_WORK")!=NULL;
 int stale=getenv("IDENTITY_MSAA_STALE_PROBE")!=NULL;
 int bad_timed_sample=getenv("IDENTITY_MSAA_BAD_TIMED_SAMPLE")!=NULL;
 if(!c->prepare || !c->validate || c->packet->sample || !c->active_samples)return 0;
 passes=c->mode?c->active_samples:1;
 begin.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
 render.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;render.renderPass=c->pass;
 render.framebuffer=c->framebuffer;render.renderArea.extent.width=c->width;render.renderArea.extent.height=c->height;
 viewport.width=(float)c->width;viewport.height=(float)c->height;viewport.maxDepth=1;
 scissor.extent.width=c->width;scissor.extent.height=c->height;
 for(work=0;work<2;++work)for(trial=0;trial<8;++trial)for(order=0;order<2;++order) {
  reference=((order^(trial&1))==0)!=reverse;
  repeated=work?512:64;multiplier=!reference && extra?2:1;
  if(!work && !trial) {
   if(!c->prepare(c->validation_context))return 0;
   if(stale)return c->validate(c->validation_context);
  }
  IW_OK(g.vkResetCommandPool(g.device,c->pool,0));
  IW_OK(g.vkBeginCommandBuffer(iw.marks[0],&begin));iw.reset(iw.marks[0],iw.query,0,2);
  iw.reset(iw.marks[0],iw.stats,0,1);iw.stamp(iw.marks[0],VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,iw.query,0);IW_OK(g.vkEndCommandBuffer(iw.marks[0]));
  IW_OK(g.vkBeginCommandBuffer(iw.marks[1],&begin));iw.stamp(iw.marks[1],VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,iw.query,1);IW_OK(g.vkEndCommandBuffer(iw.marks[1]));
  IW_OK(g.vkBeginCommandBuffer(c->command,&begin));
  transition(c->command,c->destination,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
   VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
  if(c->dynamic_rendering)begin_dynamic_image(c->command,c->numeric_view,c->width,c->height);
  else g.vkCmdBeginRenderPass(c->command,&render,VK_SUBPASS_CONTENTS_INLINE);
  im.viewport(c->command,0,1,&viewport);im.scissor(c->command,0,1,&scissor);
  g.vkCmdBindVertexBuffers(c->command,0,1,&c->vertices,&offset);
  g.vkCmdBindPipeline(c->command,VK_PIPELINE_BIND_POINT_GRAPHICS,c->pipeline[reference][0]);
  g.vkCmdBindDescriptorSets(c->command,VK_PIPELINE_BIND_POINT_GRAPHICS,c->layout[reference],0,1,&c->image,0,NULL);
  if(reference)g.vkCmdPushConstants(c->command,c->layout[reference],VK_SHADER_STAGE_FRAGMENT_BIT,0,4,&c->packed);
  iw.begin(c->command,iw.stats,0,0);
  for(draw=0;draw<repeated*multiplier;++draw)for(logical=0;logical<passes;++logical) {
   physical=c->msaa==1 && !c->native?logical*3:logical;
   if(!c->pipeline[reference][logical])return 0;
   g.vkCmdBindPipeline(c->command,VK_PIPELINE_BIND_POINT_GRAPHICS,c->pipeline[reference][logical]);
   if(!reference) {
    if(!logical)g.vkCmdPushConstants(c->command,c->layout[0],VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(*c->packet),c->packet);
    else g.vkCmdPushConstants(c->command,c->layout[0],VK_SHADER_STAGE_FRAGMENT_BIT,16,4,&physical);
    if(c->mode && c->msaa==2 && bad_timed_sample) {
     physical=(physical+1)&3;g.vkCmdPushConstants(c->command,c->layout[0],VK_SHADER_STAGE_FRAGMENT_BIT,16,4,&physical);
    }
   }
   g.vkCmdDraw(c->command,6,1,0,0);
  }
  iw.end(c->command,iw.stats,0);
  if(c->dynamic_rendering)im.end_rendering(c->command);else g.vkCmdEndRenderPass(c->command);
  transition(c->command,c->destination,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
  IW_OK(g.vkEndCommandBuffer(c->command));IW_OK(g.vkResetFences(g.device,1,&c->fence));
  submits[0].sType=submits[1].sType=submits[2].sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submits[0].commandBufferCount=1;submits[0].pCommandBuffers=&iw.marks[0];submits[0].signalSemaphoreCount=1;submits[0].pSignalSemaphores=&iw.gates[0];
  submits[1].waitSemaphoreCount=1;submits[1].pWaitSemaphores=&iw.gates[0];submits[1].pWaitDstStageMask=&wait_stage;
  submits[1].commandBufferCount=1;submits[1].pCommandBuffers=&c->command;submits[1].signalSemaphoreCount=1;submits[1].pSignalSemaphores=&iw.gates[1];
  submits[2].waitSemaphoreCount=1;submits[2].pWaitSemaphores=&iw.gates[1];submits[2].pWaitDstStageMask=&wait_stage;
  submits[2].commandBufferCount=1;submits[2].pCommandBuffers=&iw.marks[1];
  started=xe_compile_now();
  if(g.vkQueueSubmit(c->queue,3,submits,c->fence)!=VK_SUCCESS ||
   g.vkWaitForFences(g.device,1,&c->fence,VK_TRUE,UINT64_C(10000000000))!=VK_SUCCESS) {
   fprintf(stderr,"Pending numeric GPU benchmark work retained\n");exit(1);
  }
  host=xe_compile_now()-started;memset(ticks,0,sizeof(ticks));memset(stats,0,sizeof(stats));
  IW_OK(iw.results(g.device,iw.query,0,2,sizeof(ticks),ticks,2*sizeof(uint64_t),VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT));
  if(!xe_test_gpu_timestamp_seconds(ticks,iw.bits,iw.period,host,&seconds))return 0;
  IW_OK(iw.results(g.device,iw.stats,0,1,sizeof(stats),stats,sizeof(stats),VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT));
  if(!stats[3] || stats[0]!=(uint64_t)2*repeated*multiplier*passes || !stats[1] || stats[2]<(uint64_t)c->width*c->height*repeated*multiplier*c->active_samples)return 0;
  fprintf(iw.csv,"%u,%u,%u,%u,%u,%u,%u,%llu,%llu,%llu,%llu,%u,%.17g,%.17g,%.17g,%llu,%llu,%llu,%llu,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",
   c->view,c->scale,c->width*c->height,repeated,trial,order,reference,
   (unsigned long long)ticks[0],(unsigned long long)ticks[1],(unsigned long long)ticks[2],(unsigned long long)ticks[3],
   iw.bits,(double)iw.period,host,seconds,(unsigned long long)stats[0],(unsigned long long)stats[1],
   (unsigned long long)stats[2],(unsigned long long)stats[3],multiplier,c->msaa,c->native,c->mode,c->samples,c->active_samples,
   c->cap_native2,c->cap_shading,c->dynamic_rendering,c->tiles_x,c->tiles_y,c->width,c->height,c->source_msaa,c->source_samples);
  if(!work && !trial && !c->validate(c->validation_context))return 0;
 }
 return fflush(iw.csv)==0;
}
