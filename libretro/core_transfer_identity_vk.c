/* Startup-only shared float modules and final typed PSO storage. BSD; C89. */
#define VK_NO_PROTOTYPES
#include "core_transfer_identity_vk.h"
#include "core_draw_stats.h"
#include <string.h>
const VkPipeline *xe_transfer_identity_vk_get(const xe_transfer_identity_vk_bank *bank,
 unsigned view,unsigned dest,unsigned source,unsigned dest_native,unsigned source_native)
{
 unsigned group,index;
 if(!bank || !bank->ready || view>=XE_TRANSFER_IDENTITY_VIEWS || dest>2 || source>2 ||
  dest_native>1 || source_native>1 || !(bank->sample_mask[view]&(1u<<dest)) ||
  !(bank->sample_mask[view]&(1u<<source)))return NULL;
 group=(bank->scale_classes==1?0:dest_native*2+source_native)*4+view;
 index=bank->sample_shading?(group*3+dest)*3+source:
  group*21+((1u<<dest)-1)*3+source*(1u<<dest);
 return bank->pipeline+index;
}
int xe_transfer_identity_vk_close(xe_transfer_identity_vk_bank *bank,
 const xe_transfer_identity_vk_ops *ops)
{
 unsigned i;
 if(!bank)return 0;
 if(bank->device) {
  if(!ops || !ops->destroy_pipeline || !ops->destroy_layout)return 0;
  for(i=0;i<XE_TRANSFER_IDENTITY_VK_PIPELINES;++i)if(bank->pipeline[i])
   ops->destroy_pipeline(bank->device,bank->pipeline[i],NULL);
  if(bank->layout)ops->destroy_layout(bank->device,bank->layout,NULL);
 }
 memset(bank,0,sizeof(*bank));return 1;
}
int xe_transfer_identity_vk_init(xe_transfer_identity_vk_bank *bank,
 const xe_transfer_identity_vk_config *config,const xe_transfer_identity_vk_ops *ops,
 xe_spv_binary *scratch)
{
 static const VkFormat formats[4]={VK_FORMAT_R8G8B8A8_UNORM,VK_FORMAT_R16G16B16A16_UNORM,
  VK_FORMAT_A2B10G10R10_UNORM_PACK32,VK_FORMAT_R16G16B16A16_SFLOAT};
 VkShaderModule modules[9]={0},created_shader=VK_NULL_HANDLE;VkShaderModuleCreateInfo module={0};
 VkPipelineLayout created_layout=VK_NULL_HANDLE;VkPipelineLayoutCreateInfo layout={0};
 VkPushConstantRange push={VK_SHADER_STAGE_FRAGMENT_BIT,0,20};
 VkGraphicsPipelineCreateInfo create[4]={{0}};VkPipelineShaderStageCreateInfo stages[4][2]={{{0}}};
 VkPipelineMultisampleStateCreateInfo multisample[4]={{0}};
 VkPipelineRenderingCreateInfo rendering[4]={{0}};VkSampleMask masks[4];
 VkVertexInputBindingDescription binding={0};VkVertexInputAttributeDescription attribute={0};
 VkPipelineVertexInputStateCreateInfo vertex={0};VkPipelineInputAssemblyStateCreateInfo assembly={0};
 VkPipelineViewportStateCreateInfo viewport={0};VkPipelineRasterizationStateCreateInfo raster={0};
 VkPipelineColorBlendAttachmentState attachment={0};VkPipelineColorBlendStateCreateInfo blend={0};
 VkDynamicState dynamic_values[2]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
 VkPipelineDynamicStateCreateInfo dynamic={0};
 xe_transfer_address_key key={0};xe_transfer_identity_options options={0};
 unsigned f,dm,sm,sample,count,i,j,physical,scale,group,offset,needed;int success=0;
 if(!bank || bank->device || !config || !ops || !scratch || !config->device ||
  !config->image_layout || !config->vertex || config->native2>1 || config->sample_shading>1 ||
  config->dynamic_rendering>1 || !config->scale_x || !config->scale_y ||
  config->scale_x>UINT32_MAX/80 || config->scale_y>UINT32_MAX/16 ||
  !ops->create_shader || !ops->destroy_shader || !ops->create_layout ||
  !ops->destroy_layout || !ops->create_pipelines || !ops->destroy_pipeline)return 0;
 for(f=0;f<4;++f) {
  if(config->sample_mask[f]>7)return 0;
  if(!config->dynamic_rendering)for(dm=0;dm<3;++dm)
   if((config->sample_mask[f]&(1u<<dm)) && !config->passes[f][dm])return 0;
 }
 bank->device=config->device;bank->native2=config->native2;bank->sample_shading=config->sample_shading;
 bank->scale_classes=config->scale_x==1 && config->scale_y==1?1:4;
 for(f=0;f<4;++f)bank->sample_mask[f]=config->sample_mask[f];
 layout.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;layout.setLayoutCount=1;layout.pSetLayouts=&config->image_layout;
 layout.pushConstantRangeCount=1;layout.pPushConstantRanges=&push;
 if(ops->create_layout(bank->device,&layout,NULL,&created_layout)!=VK_SUCCESS || !created_layout)goto done;
 bank->layout=created_layout;
 binding.stride=16;binding.inputRate=VK_VERTEX_INPUT_RATE_INSTANCE;attribute.format=VK_FORMAT_R32G32B32A32_SFLOAT;
 vertex.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;vertex.vertexBindingDescriptionCount=vertex.vertexAttributeDescriptionCount=1;
 vertex.pVertexBindingDescriptions=&binding;vertex.pVertexAttributeDescriptions=&attribute;
 assembly.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
 viewport.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;viewport.viewportCount=viewport.scissorCount=1;
 raster.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;raster.polygonMode=VK_POLYGON_MODE_FILL;raster.lineWidth=1;
 attachment.colorWriteMask=15;blend.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;blend.attachmentCount=1;blend.pAttachments=&attachment;
 dynamic.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;dynamic.dynamicStateCount=2;dynamic.pDynamicStates=dynamic_values;
 key.source_color=key.dest_color=1;key.source_pitch=key.dest_pitch=1;
 key.source_native2=key.dest_native2=bank->native2;
 for(scale=0;scale<bank->scale_classes;++scale) {
  key.source_scale_x=scale&1?1:config->scale_x;key.source_scale_y=scale&1?1:config->scale_y;
  key.dest_scale_x=scale&2?1:config->scale_x;key.dest_scale_y=scale&2?1:config->scale_y;
  for(dm=0;dm<3;++dm)for(sm=0;sm<3;++sm) {
   needed=0;for(f=0;f<4;++f)if((bank->sample_mask[f]&(1u<<dm)) &&
    (bank->sample_mask[f]&(1u<<sm)))needed=1;
   if(!needed)continue;
   key.dest_msaa=dm;key.source_msaa=sm;
   options.source_multisampled=sm!=0;options.sample_mode=!dm?XE_TRANSFER_IDENTITY_SINGLE_SAMPLE:
    bank->sample_shading?XE_TRANSFER_IDENTITY_SAMPLE_BUILTIN:XE_TRANSFER_IDENTITY_SAMPLE_CONSTANT;
   if(!xe_transfer_identity_spv_build(scratch,&options,&key))goto done;
   module.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;module.codeSize=scratch->count*sizeof(uint32_t);module.pCode=scratch->words;
   created_shader=VK_NULL_HANDLE;
   XE_DRAW_EVENT(XE_DRAW_UTILITY_PIPELINE,XE_DRAW_MODULE);
   if(ops->create_shader(bank->device,&module,NULL,&created_shader)!=VK_SUCCESS || !created_shader)goto done;
   modules[dm*3+sm]=created_shader;
  }
  for(f=0;f<4;++f)for(dm=0;dm<3;++dm)for(sm=0;sm<3;++sm) {
   if(!(bank->sample_mask[f]&(1u<<dm)) || !(bank->sample_mask[f]&(1u<<sm)))continue;
   count=bank->sample_shading?1:1u<<dm;
   group=scale*4+f;offset=bank->sample_shading?(group*3+dm)*3+sm:
    group*21+((1u<<dm)-1)*3+sm*(1u<<dm);
   if(offset+count>XE_TRANSFER_IDENTITY_VK_PIPELINES)goto done;
   for(sample=0;sample<count;++sample) {
    physical=dm==1 && !bank->native2?sample*3:sample;
    masks[sample]=!dm?1:bank->sample_shading?(dm==1 && !bank->native2?9:UINT32_MAX):1u<<physical;
    for(j=0;j<2;++j){stages[sample][j].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;stages[sample][j].pName="main";}
    stages[sample][0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[sample][0].module=config->vertex;
    stages[sample][1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;stages[sample][1].module=modules[dm*3+sm];
    multisample[sample].sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample[sample].rasterizationSamples=(VkSampleCountFlagBits)(dm==1 && !bank->native2?4:1u<<dm);
    multisample[sample].sampleShadingEnable=dm!=0 && bank->sample_shading;multisample[sample].minSampleShading=1;
    multisample[sample].pSampleMask=&masks[sample];
    create[sample].sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;create[sample].stageCount=2;create[sample].pStages=stages[sample];
    create[sample].pVertexInputState=&vertex;create[sample].pInputAssemblyState=&assembly;create[sample].pViewportState=&viewport;
    create[sample].pRasterizationState=&raster;create[sample].pMultisampleState=&multisample[sample];
    create[sample].pColorBlendState=&blend;create[sample].pDynamicState=&dynamic;create[sample].layout=bank->layout;
    if(config->dynamic_rendering) {
     rendering[sample].sType=VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;rendering[sample].colorAttachmentCount=1;
     rendering[sample].pColorAttachmentFormats=&formats[f];create[sample].pNext=&rendering[sample];
    } else create[sample].renderPass=config->passes[f][dm];
   }
#if XE_GPU_DRAW_STATS
   for(i=0;i<count;++i)XE_DRAW_EVENT(XE_DRAW_UTILITY_PIPELINE,XE_DRAW_GRAPHICS_PSO);
#endif
   if(ops->create_pipelines(bank->device,VK_NULL_HANDLE,count,create,NULL,bank->pipeline+offset)!=VK_SUCCESS)goto done;
   for(i=0;i<count;++i)if(!bank->pipeline[offset+i])goto done;
  }
  for(i=0;i<9;++i)if(modules[i]) {
   ops->destroy_shader(bank->device,modules[i],NULL);modules[i]=VK_NULL_HANDLE;
  }
 }
 bank->ready=1;success=1;
done:
 for(i=0;i<9;++i)if(modules[i])ops->destroy_shader(bank->device,modules[i],NULL);
 if(!success)xe_transfer_identity_vk_close(bank,ops);
 return success;
}
