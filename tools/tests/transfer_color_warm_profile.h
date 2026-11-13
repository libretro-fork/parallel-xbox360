/* Test-only paired GPU work. Included only by XE_COLOR_WARM_BENCH builds. */
#include "gpu_timestamp_measure.h"
static struct {
 VkQueryPool stamps,stats;VkSemaphore gates[2];VkCommandBuffer marks[2];
 PFN_vkCreateQueryPool create_query;PFN_vkDestroyQueryPool destroy_query;
 PFN_vkGetQueryPoolResults get_query;PFN_vkCmdResetQueryPool reset_query;
 PFN_vkCmdWriteTimestamp timestamp;PFN_vkCmdBeginQuery begin_query;PFN_vkCmdEndQuery end_query;
 PFN_vkCreateSemaphore create_semaphore;PFN_vkDestroySemaphore destroy_semaphore;
 unsigned bits,dynamic_rendering;float period;FILE *csv;
} color_warm;
#define CW_LOAD(field,name) do{PFN_vkVoidFunction f=g.get(g.instance,#name);memcpy(&color_warm.field,&f,sizeof(f));if(!color_warm.field)return 0;}while(0)
static int color_warm_init(VkCommandPool pool,unsigned bits,float period){
 VkQueryPoolCreateInfo query={0};VkSemaphoreCreateInfo semaphore={0};VkCommandBufferAllocateInfo commands={0};unsigned i;
 VkQueryPool query_result=VK_NULL_HANDLE;VkSemaphore semaphore_result=VK_NULL_HANDLE;
 const char *csv=getenv("COLOR_GPU_CSV");
 if(!csv || bits<36 || bits>64 || !(period>0))return 0;
 color_warm.bits=bits;color_warm.period=period;
 CW_LOAD(create_query,vkCreateQueryPool);CW_LOAD(destroy_query,vkDestroyQueryPool);CW_LOAD(get_query,vkGetQueryPoolResults);
 CW_LOAD(reset_query,vkCmdResetQueryPool);CW_LOAD(timestamp,vkCmdWriteTimestamp);CW_LOAD(begin_query,vkCmdBeginQuery);CW_LOAD(end_query,vkCmdEndQuery);
 CW_LOAD(create_semaphore,vkCreateSemaphore);CW_LOAD(destroy_semaphore,vkDestroySemaphore);
 query.sType=VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;query.queryType=VK_QUERY_TYPE_TIMESTAMP;query.queryCount=2;
 if(color_warm.create_query(g.device,&query,NULL,&query_result)!=VK_SUCCESS || !query_result)return 0;
 color_warm.stamps=query_result;query_result=VK_NULL_HANDLE;
 query.queryType=VK_QUERY_TYPE_PIPELINE_STATISTICS;query.queryCount=1;
 query.pipelineStatistics=VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_PRIMITIVES_BIT|VK_QUERY_PIPELINE_STATISTIC_VERTEX_SHADER_INVOCATIONS_BIT|VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT;
 if(color_warm.create_query(g.device,&query,NULL,&query_result)!=VK_SUCCESS || !query_result)return 0;
 color_warm.stats=query_result;
 semaphore.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
 for(i=0;i<2;++i){
  semaphore_result=VK_NULL_HANDLE;
  if(color_warm.create_semaphore(g.device,&semaphore,NULL,&semaphore_result)!=VK_SUCCESS || !semaphore_result)return 0;
  color_warm.gates[i]=semaphore_result;
 }
 commands.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;commands.commandPool=pool;commands.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;commands.commandBufferCount=2;
 if(g.vkAllocateCommandBuffers(g.device,&commands,color_warm.marks)!=VK_SUCCESS)return 0;
 color_warm.csv=fopen(csv,"wb");if(!color_warm.csv)return 0;
 fputs("scale,source,dest,width,height,repeat,trial,order,reference,start,available_start,end,available_end,bits,period_ns,host_seconds,gpu_seconds,ia_primitives,vs_invocations,fs_invocations,available_stats,dynamic_rendering,packed_address,same_pipeline\n",color_warm.csv);return 1;
}
static void color_warm_close(void){
 unsigned i;if(color_warm.csv)fclose(color_warm.csv);
 if(color_warm.stamps)color_warm.destroy_query(g.device,color_warm.stamps,NULL);
 if(color_warm.stats)color_warm.destroy_query(g.device,color_warm.stats,NULL);
 for(i=0;i<2;++i)if(color_warm.gates[i])color_warm.destroy_semaphore(g.device,color_warm.gates[i],NULL);
 memset(&color_warm,0,sizeof(color_warm));
}
typedef struct color_warm_case {
 VkCommandPool pool;VkCommandBuffer command;VkQueue queue;VkFence fence;
 VkRenderPass pass;VkFramebuffer framebuffer;VkBuffer vertices;VkDescriptorSet descriptor;
 VkPipeline pipeline[2];VkPipelineLayout layout[2];image *dest;
 const xe_transfer_color_packet *packet;uint32_t legacy_packet;
 unsigned width,height,scale,source_format,dest_format,dynamic_rendering,packed_address,same_pipeline;
} color_warm_case;
#define CW_OK(x) do{if((x)!=VK_SUCCESS){fprintf(stderr,"Color warm API line%d\n",__LINE__);return 0;}}while(0)
static int color_warm_profile(const color_warm_case *c){
 VkCommandBufferBeginInfo begin={0};VkSubmitInfo submits[3]={{0},{0},{0}};
 VkPipelineStageFlags wait_stage=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
 VkViewport viewport={0};VkRect2D scissor={{0,0},{0,0}};VkDeviceSize offset=0;
 uint64_t ticks[4],stats[4];unsigned trial,order,ref,work,n,repeat;
 double started,host,seconds;
 begin.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
 if(c->dynamic_rendering!=color_warm.dynamic_rendering)return 0;
 viewport.width=(float)c->width;viewport.height=(float)c->height;viewport.maxDepth=1;
 scissor.extent.width=c->width;scissor.extent.height=c->height;
 for(work=0;work<2;++work)for(trial=0;trial<8;++trial)for(order=0;order<2;++order){
  ref=((order^(trial&1))==0)!=(getenv("COLOR_REVERSE_ORDER")!=NULL);repeat=work?512:64;
#ifdef XE_COLOR_WARM_SHORT_CONTROL
  if(!c->same_pipeline)return 0;
  repeat=work?64:8;
#endif
  CW_OK(g.vkResetCommandPool(g.device,c->pool,0));
  CW_OK(g.vkBeginCommandBuffer(color_warm.marks[0],&begin));color_warm.reset_query(color_warm.marks[0],color_warm.stamps,0,2);color_warm.reset_query(color_warm.marks[0],color_warm.stats,0,1);
  color_warm.timestamp(color_warm.marks[0],VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,color_warm.stamps,0);CW_OK(g.vkEndCommandBuffer(color_warm.marks[0]));
  CW_OK(g.vkBeginCommandBuffer(color_warm.marks[1],&begin));color_warm.timestamp(color_warm.marks[1],VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,color_warm.stamps,1);CW_OK(g.vkEndCommandBuffer(color_warm.marks[1]));
  CW_OK(g.vkBeginCommandBuffer(c->command,&begin));
  transition(c->command,c->dest->image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
  color_begin_pass(c->command,c->pass,c->framebuffer,c->dest->view,c->width,c->height,c->dynamic_rendering);
  im.viewport(c->command,0,1,&viewport);im.scissor(c->command,0,1,&scissor);
  g.vkCmdBindVertexBuffers(c->command,0,1,&c->vertices,&offset);g.vkCmdBindPipeline(c->command,VK_PIPELINE_BIND_POINT_GRAPHICS,c->pipeline[ref]);
  g.vkCmdBindDescriptorSets(c->command,VK_PIPELINE_BIND_POINT_GRAPHICS,c->layout[ref],0,1,&c->descriptor,0,NULL);
  if((ref && !c->same_pipeline) || c->packed_address)g.vkCmdPushConstants(c->command,c->layout[ref],VK_SHADER_STAGE_FRAGMENT_BIT,0,4,&c->legacy_packet);
  else g.vkCmdPushConstants(c->command,c->layout[ref],VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(*c->packet),c->packet);
  color_warm.begin_query(c->command,color_warm.stats,0,0);
  for(n=0;n<repeat;++n)if(!getenv("COLOR_OMIT_DRAW"))g.vkCmdDraw(c->command,6,1,0,0);
  color_warm.end_query(c->command,color_warm.stats,0);color_end_pass(c->command,c->dynamic_rendering);
  transition(c->command,c->dest->image,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
  CW_OK(g.vkEndCommandBuffer(c->command));CW_OK(g.vkResetFences(g.device,1,&c->fence));
  submits[0].sType=submits[1].sType=submits[2].sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submits[0].commandBufferCount=1;submits[0].pCommandBuffers=color_warm.marks;submits[0].signalSemaphoreCount=1;submits[0].pSignalSemaphores=color_warm.gates;
  submits[1].waitSemaphoreCount=1;submits[1].pWaitSemaphores=color_warm.gates;submits[1].pWaitDstStageMask=&wait_stage;submits[1].commandBufferCount=1;submits[1].pCommandBuffers=&c->command;submits[1].signalSemaphoreCount=1;submits[1].pSignalSemaphores=color_warm.gates+1;
  submits[2].waitSemaphoreCount=1;submits[2].pWaitSemaphores=color_warm.gates+1;submits[2].pWaitDstStageMask=&wait_stage;submits[2].commandBufferCount=1;submits[2].pCommandBuffers=color_warm.marks+1;
  started=xe_compile_now();
  if(g.vkQueueSubmit(c->queue,3,submits,c->fence)!=VK_SUCCESS || g.vkWaitForFences(g.device,1,&c->fence,VK_TRUE,UINT64_C(10000000000))!=VK_SUCCESS){fputs("Pending color work retained\n",stderr);exit(1);}
  host=xe_compile_now()-started;memset(ticks,0,sizeof(ticks));
  CW_OK(color_warm.get_query(g.device,color_warm.stamps,0,2,sizeof(ticks),ticks,2*sizeof(uint64_t),VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT));
  if(!xe_test_gpu_timestamp_seconds(ticks,color_warm.bits,color_warm.period,host,&seconds))return 0;
  memset(stats,0,sizeof(stats));CW_OK(color_warm.get_query(g.device,color_warm.stats,0,1,sizeof(stats),stats,sizeof(stats),VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT));
  if(!stats[3] || stats[0]!=(uint64_t)2*repeat || !stats[1] || stats[2]<(uint64_t)c->width*c->height*repeat){
   fprintf(stderr,"Color warm work mismatch: IA=%llu VS=%llu FS=%llu available=%llu repeat=%u\n",
    (unsigned long long)stats[0],(unsigned long long)stats[1],(unsigned long long)stats[2],(unsigned long long)stats[3],repeat);return 0;
  }
  fprintf(color_warm.csv,"%u,%u,%u,%u,%u,%u,%u,%u,%u,%llu,%llu,%llu,%llu,%u,%.17g,%.17g,%.17g,%llu,%llu,%llu,%llu,%u,%u,%u\n",
   c->scale,c->source_format,c->dest_format,c->width,c->height,repeat,trial,order,ref,
   (unsigned long long)ticks[0],(unsigned long long)ticks[1],(unsigned long long)ticks[2],(unsigned long long)ticks[3],color_warm.bits,(double)color_warm.period,host,seconds,
   (unsigned long long)stats[0],(unsigned long long)stats[1],(unsigned long long)stats[2],(unsigned long long)stats[3],c->dynamic_rendering,c->packed_address,c->same_pipeline);
 }
 return fflush(color_warm.csv)==0;
}
