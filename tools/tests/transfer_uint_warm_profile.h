/* Procedural warmed-transfer measurement only; BSD; MSVC C89. */
#include "gpu_timestamp_measure.h"
#include "transfer_uint_revision_load.h"
static const unsigned warm_pairs[10][2]={{4,6},{6,4},{5,7},{7,5},{14,4},{4,14},{15,5},{5,15},{4,5},{5,4}};
static int warm_pair(unsigned sf,unsigned df) {
 unsigned i;for(i=0;i<10;++i)if(warm_pairs[i][0]==sf && warm_pairs[i][1]==df)return (int)i;
 return -1;
}
static struct {
 VkPipeline legacy[10][3][3];VkPipelineLayout layout;
 xe_test_transfer_revision_library revision;void *parent_packet;
 VkQueryPool queries,statistics;VkSemaphore gates[2];VkCommandBuffer marks[2];
 unsigned bits;float period;FILE *csv;
 PFN_vkCreateQueryPool create_query;PFN_vkDestroyQueryPool destroy_query;
 PFN_vkGetQueryPoolResults get_query;PFN_vkCmdResetQueryPool reset_query;
 PFN_vkCmdWriteTimestamp write_stamp;
 PFN_vkCmdBeginQuery begin_query;PFN_vkCmdEndQuery end_query;
 PFN_vkCreateSemaphore create_semaphore;PFN_vkDestroySemaphore destroy_semaphore;
} warm;
#define WARM_LOAD(field,name) do {PFN_vkVoidFunction fn=g.get(g.instance,#name);memcpy(&warm.field,&fn,sizeof(fn));if(!warm.field)return 0;}while(0)
static int warm_init(const bank *b,VkDescriptorSetLayout image_layout,VkCommandPool pool,unsigned bits,float period) {
 VkPipelineLayoutCreateInfo layout={0};VkPushConstantRange push={VK_SHADER_STAGE_FRAGMENT_BIT,0,4};
 VkPipelineShaderStageCreateInfo stages[2]={{0},{0}};VkVertexInputBindingDescription binding={0};VkVertexInputAttributeDescription attribute={0};
 VkPipelineVertexInputStateCreateInfo vertex={0};VkPipelineInputAssemblyStateCreateInfo assembly={0};VkPipelineViewportStateCreateInfo viewport={0};
 VkPipelineRasterizationStateCreateInfo raster={0};VkPipelineMultisampleStateCreateInfo multisample={0};
 VkPipelineColorBlendAttachmentState attachment={0};VkPipelineColorBlendStateCreateInfo blend={0};
 VkDynamicState values[2]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};VkPipelineDynamicStateCreateInfo dynamic={0};
 VkGraphicsPipelineCreateInfo pipeline={0};VkShaderModule fragment=VK_NULL_HANDLE;
 VkPipelineLayout layout_result=VK_NULL_HANDLE;VkQueryPool query_result=VK_NULL_HANDLE;VkSemaphore semaphore_result=VK_NULL_HANDLE;
 VkQueryPoolCreateInfo query={0};VkSemaphoreCreateInfo semaphore={0};VkCommandBufferAllocateInfo commands={0};
 xe_test_transfer_revision_config config={0};
 unsigned p,m,s,i;char path[4096];const char *parent=getenv("TRANSFER_PARENT_LIBRARY");const char *prefix=getenv("TRANSFER_LEGACY_PREFIX"),*csv=getenv("TRANSFER_GPU_CSV");
 if((!parent && (!prefix || strlen(prefix)>3000)) || bits<36 || bits>64 || !(period>0))return 0;
 warm.bits=bits;warm.period=period;
 WARM_LOAD(create_query,vkCreateQueryPool);WARM_LOAD(destroy_query,vkDestroyQueryPool);WARM_LOAD(get_query,vkGetQueryPoolResults);WARM_LOAD(reset_query,vkCmdResetQueryPool);WARM_LOAD(write_stamp,vkCmdWriteTimestamp);WARM_LOAD(create_semaphore,vkCreateSemaphore);WARM_LOAD(destroy_semaphore,vkDestroySemaphore);WARM_LOAD(begin_query,vkCmdBeginQuery);WARM_LOAD(end_query,vkCmdEndQuery);
 if(parent) {
  if(!xe_test_transfer_revision_load(&warm.revision,parent))return 0;
  warm.parent_packet=malloc(XE_TEST_TRANSFER_PACKET_BYTES);if(!warm.parent_packet)return 0;
  config.device=g.device;config.images=image_layout;config.vertex=b->vertex;
  config.native2=config.sample_shading=1;
  config.create_shader=g.vkCreateShaderModule;config.destroy_shader=g.vkDestroyShaderModule;
  config.create_layout=g.vkCreatePipelineLayout;config.destroy_layout=g.vkDestroyPipelineLayout;
  config.create_pipelines=g.vkCreateGraphicsPipelines;config.destroy_pipeline=g.vkDestroyPipeline;
  for(p=0;p<4;++p)for(m=0;m<3;++m)config.passes[p][m]=b->render[p][m];
  for(s=0;s<3;++s) {
   config.scale_x=config.scale_y=1u<<s;
   warm.revision.owner[s]=warm.revision.api->open(&config);
   if(!warm.revision.owner[s])return 0;
   warm.revision.layout[s]=warm.revision.api->layout(warm.revision.owner[s]);
   if(!warm.revision.layout[s])return 0;
   for(p=0;p<10;++p)for(m=0;m<3;++m) {
    warm.legacy[p][m][s]=warm.revision.api->pipeline(warm.revision.owner[s],warm_pairs[p][1],m,m);
    if(!warm.legacy[p][m][s])return 0;
   }
  }
  fprintf(stderr,"TRANSFER_PARENT_LIBRARY %s abi%u packet%u\n",parent,warm.revision.api->version,warm.revision.api->packet_bytes);
 } else {
 layout.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;layout.setLayoutCount=1;layout.pSetLayouts=&image_layout;layout.pushConstantRangeCount=1;layout.pPushConstantRanges=&push;
 if(g.vkCreatePipelineLayout(g.device,&layout,NULL,&layout_result)!=VK_SUCCESS || !layout_result)return 0;
 warm.layout=layout_result;
 stages[0].sType=stages[1].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[0].module=b->vertex;stages[0].pName=stages[1].pName="main";stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;
 binding.stride=16;binding.inputRate=VK_VERTEX_INPUT_RATE_INSTANCE;attribute.format=VK_FORMAT_R32G32B32A32_SFLOAT;
 vertex.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;vertex.vertexBindingDescriptionCount=vertex.vertexAttributeDescriptionCount=1;vertex.pVertexBindingDescriptions=&binding;vertex.pVertexAttributeDescriptions=&attribute;
 assembly.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
 viewport.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;viewport.viewportCount=viewport.scissorCount=1;
 raster.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;raster.polygonMode=VK_POLYGON_MODE_FILL;raster.lineWidth=1;
 multisample.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;multisample.minSampleShading=1;
 attachment.colorWriteMask=15;blend.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;blend.attachmentCount=1;blend.pAttachments=&attachment;
 dynamic.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;dynamic.dynamicStateCount=2;dynamic.pDynamicStates=values;
 pipeline.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;pipeline.stageCount=2;pipeline.pStages=stages;pipeline.pVertexInputState=&vertex;pipeline.pInputAssemblyState=&assembly;pipeline.pViewportState=&viewport;pipeline.pRasterizationState=&raster;pipeline.pMultisampleState=&multisample;pipeline.pColorBlendState=&blend;pipeline.pDynamicState=&dynamic;pipeline.layout=warm.layout;
 for(p=0;p<10;++p)for(m=0;m<3;++m)for(s=0;s<3;++s) {
  sprintf(path,"%s-p%u-m%u-s%u.spv",prefix,p,m,1u<<s);if(!shader(path,&fragment))return 0;
  stages[1].module=fragment;multisample.rasterizationSamples=(VkSampleCountFlagBits)(1u<<m);multisample.sampleShadingEnable=m!=0;
  pipeline.renderPass=b->render[format_index(warm_pairs[p][1])][m];
  if(g.vkCreateGraphicsPipelines(g.device,VK_NULL_HANDLE,1,&pipeline,NULL,&warm.legacy[p][m][s])!=VK_SUCCESS){g.vkDestroyShaderModule(g.device,fragment,NULL);return 0;}
  g.vkDestroyShaderModule(g.device,fragment,NULL);fragment=VK_NULL_HANDLE;


 }
 }
 query.sType=VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;query.queryType=VK_QUERY_TYPE_TIMESTAMP;query.queryCount=2;
 if(warm.create_query(g.device,&query,NULL,&query_result)!=VK_SUCCESS || !query_result)return 0;
 warm.queries=query_result;query_result=VK_NULL_HANDLE;
 query.queryType=VK_QUERY_TYPE_PIPELINE_STATISTICS;query.queryCount=1;
 query.pipelineStatistics=VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_PRIMITIVES_BIT|VK_QUERY_PIPELINE_STATISTIC_VERTEX_SHADER_INVOCATIONS_BIT|VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT;
 if(warm.create_query(g.device,&query,NULL,&query_result)!=VK_SUCCESS || !query_result)return 0;
 warm.statistics=query_result;
 semaphore.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
 for(i=0;i<2;++i) {
  semaphore_result=VK_NULL_HANDLE;
  if(warm.create_semaphore(g.device,&semaphore,NULL,&semaphore_result)!=VK_SUCCESS || !semaphore_result)return 0;
  warm.gates[i]=semaphore_result;
 }
 commands.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;commands.commandPool=pool;commands.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;commands.commandBufferCount=2;
 if(g.vkAllocateCommandBuffers(g.device,&commands,warm.marks)!=VK_SUCCESS)return 0;
 if(csv) {
  warm.csv=fopen(csv,"wb");if(!warm.csv)return 0;
  fprintf(warm.csv,"pair,msaa,scale,pixels,samples,repeat,trial,order,reference,start,available_start,end,available_end,bits,period_ns,host_seconds,gpu_seconds,ia_primitives,vs_invocations,fs_invocations,available_stats,parent\n");
 }
 return 1;
}
static VkPipelineLayout warm_reference_layout(unsigned scale) {
 return warm.revision.api?warm.revision.layout[scale]:warm.layout;
}
static int warm_reference_packet(const xe_transfer_address_key *key,unsigned sf,unsigned df) {
 xe_test_transfer_revision_key input;
 if(!warm.revision.api)return 1;
 input.dest_msaa=key->dest_msaa;input.source_msaa=key->source_msaa;
 input.dest_native2=key->dest_native2;input.source_native2=key->source_native2;
 input.dest_scale_x=key->dest_scale_x;input.dest_scale_y=key->dest_scale_y;
 input.source_scale_x=key->source_scale_x;input.source_scale_y=key->source_scale_y;
 input.dest_wide=key->dest_wide;input.source_wide=key->source_wide;
 input.dest_pitch=key->dest_pitch;input.source_pitch=key->source_pitch;
 input.delta=key->dest_to_source_base;input.source_format=sf;input.dest_format=df;input.sample=0;
 return warm.revision.api->packet(&input,warm.parent_packet,XE_TEST_TRANSFER_PACKET_BYTES);
}
static void warm_close(void) {
 unsigned p,m,s;
 if(warm.csv){fclose(warm.csv);warm.csv=NULL;}
 if(!g.device)return;
 if(!warm.revision.api)for(p=0;p<10;++p)for(m=0;m<3;++m)for(s=0;s<3;++s)if(warm.legacy[p][m][s])g.vkDestroyPipeline(g.device,warm.legacy[p][m][s],NULL);
 if(!xe_test_transfer_revision_unload(&warm.revision)){fprintf(stderr,"Parent bank ownership retained\n");exit(1);}
 free(warm.parent_packet);
 if(warm.layout)g.vkDestroyPipelineLayout(g.device,warm.layout,NULL);
 if(warm.queries)warm.destroy_query(g.device,warm.queries,NULL);
 if(warm.statistics)warm.destroy_query(g.device,warm.statistics,NULL);
 for(p=0;p<2;++p)if(warm.gates[p])warm.destroy_semaphore(g.device,warm.gates[p],NULL);
 memset(&warm,0,sizeof(warm));
}typedef struct warm_case {
 const bank *bank_;const xe_transfer_uint_vk_bank *ready;
 const xe_transfer_uint_packet *packet;const xe_transfer_address_key *key;
 VkCommandPool pool;VkCommandBuffer command;VkQueue queue;VkFence fence;
 VkBuffer vertices;VkDescriptorSet descriptor;VkFramebuffer framebuffer;
 image *destination;VkPipeline generic;
 unsigned width,height,pair,msaa,scale;
} warm_case;
#define WARM_OK(call) do {if((call)!=VK_SUCCESS){fprintf(stderr,"Warm GPU API failure line%u\n",(unsigned)__LINE__);return 0;}}while(0)
static int warm_profile(const warm_case *c) {
 VkCommandBufferBeginInfo begin={0};VkSubmitInfo submits[3]={{0},{0},{0}};
 VkPipelineStageFlags wait_stage=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;VkDeviceSize offset=0;
 VkPipelineLayout layout;VkPipeline pipeline;uint32_t packed;
 unsigned repetition,trial,order,reference,repeated,work_class;uint64_t ticks[4],stats[4];
 double host_started,host_seconds,gpu_seconds;
 if(!warm.csv)return 1;
 packed=c->key->dest_pitch|(c->key->source_pitch<<10)|(((uint32_t)c->key->dest_to_source_base&4095)<<20);
 begin.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
 for(work_class=0;work_class<2;++work_class)for(trial=0;trial<8;++trial)for(order=0;order<2;++order) {
  /* Trial0 warms both implementations and is retained but excluded by analysis.
   * Alternate and reverse each pair rather than timing one version in bulk. */
  reference=((order^(trial&1))==0) != (getenv("TRANSFER_REVERSE_ORDER")!=NULL);repeated=work_class?512:64;
  layout=reference?warm_reference_layout(c->scale):c->ready->layout;
  pipeline=reference?warm.legacy[c->pair][c->msaa][c->scale]:c->generic;
  WARM_OK(g.vkResetCommandPool(g.device,c->pool,0));
  WARM_OK(g.vkBeginCommandBuffer(warm.marks[0],&begin));warm.reset_query(warm.marks[0],warm.queries,0,2);warm.reset_query(warm.marks[0],warm.statistics,0,1);warm.write_stamp(warm.marks[0],VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,warm.queries,0);WARM_OK(g.vkEndCommandBuffer(warm.marks[0]));
  WARM_OK(g.vkBeginCommandBuffer(warm.marks[1],&begin));warm.write_stamp(warm.marks[1],VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,warm.queries,1);WARM_OK(g.vkEndCommandBuffer(warm.marks[1]));
  WARM_OK(g.vkBeginCommandBuffer(c->command,&begin));
  transition(c->command,c->destination->image,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
  begin_image(c->command,c->bank_->render[format_index(warm_pairs[c->pair][1])][c->msaa],c->framebuffer,c->width,c->height);
  g.vkCmdBindVertexBuffers(c->command,0,1,&c->vertices,&offset);
  g.vkCmdBindPipeline(c->command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);
  g.vkCmdBindDescriptorSets(c->command,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,0,1,&c->descriptor,0,NULL);
  if(reference)g.vkCmdPushConstants(c->command,layout,VK_SHADER_STAGE_FRAGMENT_BIT,0,
   warm.revision.api?XE_TEST_TRANSFER_PACKET_BYTES:4,warm.revision.api?warm.parent_packet:&packed);
  else g.vkCmdPushConstants(c->command,layout,VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(*c->packet),c->packet);
  warm.begin_query(c->command,warm.statistics,0,0);
  for(repetition=0;repetition<repeated;++repetition)g.vkCmdDraw(c->command,6,1,0,0);
  warm.end_query(c->command,warm.statistics,0);
  g.vkCmdEndRenderPass(c->command);
  transition(c->command,c->destination->image,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
  WARM_OK(g.vkEndCommandBuffer(c->command));WARM_OK(g.vkResetFences(g.device,1,&c->fence));
  submits[0].sType=submits[1].sType=submits[2].sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submits[0].commandBufferCount=1;submits[0].pCommandBuffers=&warm.marks[0];submits[0].signalSemaphoreCount=1;submits[0].pSignalSemaphores=&warm.gates[0];
  submits[1].waitSemaphoreCount=1;submits[1].pWaitSemaphores=&warm.gates[0];submits[1].pWaitDstStageMask=&wait_stage;submits[1].commandBufferCount=1;submits[1].pCommandBuffers=&c->command;submits[1].signalSemaphoreCount=1;submits[1].pSignalSemaphores=&warm.gates[1];
  submits[2].waitSemaphoreCount=1;submits[2].pWaitSemaphores=&warm.gates[1];submits[2].pWaitDstStageMask=&wait_stage;submits[2].commandBufferCount=1;submits[2].pCommandBuffers=&warm.marks[1];
  host_started=xe_compile_now();
  if(g.vkQueueSubmit(c->queue,3,submits,c->fence)!=VK_SUCCESS || g.vkWaitForFences(g.device,1,&c->fence,VK_TRUE,UINT64_C(10000000000))!=VK_SUCCESS){fprintf(stderr,"Warm GPU pending work retained\n");exit(1);}
  host_seconds=xe_compile_now()-host_started;memset(ticks,0,sizeof(ticks));
  WARM_OK(warm.get_query(g.device,warm.queries,0,2,sizeof(ticks),ticks,2*sizeof(uint64_t),VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT));
  if(!xe_test_gpu_timestamp_seconds(ticks,warm.bits,warm.period,host_seconds,&gpu_seconds))return 0;
  memset(stats,0,sizeof(stats));WARM_OK(warm.get_query(g.device,warm.statistics,0,1,sizeof(stats),stats,sizeof(stats),VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT));
  if(!stats[3] || stats[0]!=(uint64_t)2*repeated || !stats[1] || stats[2]<(uint64_t)c->width*c->height*(UINT64_C(1)<<c->msaa)*repeated){fprintf(stderr,"Warm GPU work count mismatch\n");return 0;}
  fprintf(warm.csv,"%u,%u,%u,%u,%u,%u,%u,%u,%u,%llu,%llu,%llu,%llu,%u,%.17g,%.17g,%.17g,%llu,%llu,%llu,%llu,%u\n",c->pair,c->msaa,1u<<c->scale,c->width*c->height,1u<<c->msaa,repeated,trial,order,reference,(unsigned long long)ticks[0],(unsigned long long)ticks[1],(unsigned long long)ticks[2],(unsigned long long)ticks[3],warm.bits,(double)warm.period,host_seconds,gpu_seconds,(unsigned long long)stats[0],(unsigned long long)stats[1],(unsigned long long)stats[2],(unsigned long long)stats[3],warm.revision.api!=NULL);
 }
 return fflush(warm.csv)==0;
}
