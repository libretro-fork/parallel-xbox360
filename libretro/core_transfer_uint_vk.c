/* Bounded reusable PSO storage, no frame-time creation. BSD; MSVC C89. */
#define VK_NO_PROTOTYPES
#include "core_transfer_uint_vk.h"
#include "core_draw_stats.h"
#include <string.h>
static int format_class(unsigned format,unsigned *out) {
 switch(format) {
  case 4:case 6:*out=0;return 1;
  case 5:case 7:*out=1;return 1;
  case 14:*out=2;return 1;
  case 15:*out=3;return 1;
  default:return 0;
 }
}
const VkPipeline *xe_transfer_uint_vk_get(const xe_transfer_uint_vk_bank *bank,
 unsigned format,unsigned msaa,unsigned source,unsigned dest_native,unsigned source_native) {
 unsigned f,index,group;
 if(!bank || !bank->ready || msaa>2 || source>2 || dest_native>1 || source_native>1 || !format_class(format,&f))return NULL;
 group=(bank->scale_classes==1?0:dest_native*2+source_native)*4+f;
 index=bank->sample_shading?(group*3+msaa)*3+source:
  group*21+((1u<<msaa)-1)*3+source*(1u<<msaa);
 return bank->pipeline+index;
}
unsigned xe_transfer_uint_vk_physical_sample(const xe_transfer_uint_vk_bank *bank,
 unsigned msaa,unsigned logical) {
 if(!bank || !bank->ready || msaa>2 || logical>=(1u<<msaa))return UINT32_MAX;
 return msaa==1 && !bank->native2?logical*3:logical;
}
int xe_transfer_uint_vk_close(xe_transfer_uint_vk_bank *bank,
 const xe_transfer_uint_vk_ops *ops) {
 unsigned i;
 if(!bank)return 0;
 if(bank->device) {
  if(!ops || !ops->destroy_pipeline || !ops->destroy_layout)return 0;
  for(i=0;i<XE_TRANSFER_UINT_VK_PIPELINES;++i)if(bank->pipeline[i])
   ops->destroy_pipeline(bank->device,bank->pipeline[i],NULL);
  if(bank->layout)ops->destroy_layout(bank->device,bank->layout,NULL);
 }
 memset(bank,0,sizeof(*bank));return 1;
}
int xe_transfer_uint_vk_init(xe_transfer_uint_vk_bank *bank,
 const xe_transfer_uint_vk_config *config,const xe_transfer_uint_vk_ops *ops,
 xe_spv_binary *scratch) {
 static const VkFormat formats[4]={VK_FORMAT_R16G16_UINT,VK_FORMAT_R16G16B16A16_UINT,VK_FORMAT_R32_UINT,VK_FORMAT_R32G32_UINT};
 VkShaderModule modules[9]={0};VkShaderModuleCreateInfo module={0};
 VkShaderModule shader_result=VK_NULL_HANDLE;VkPipelineLayout layout_result=VK_NULL_HANDLE;
 VkPipelineLayoutCreateInfo layout={0};VkPushConstantRange push={VK_SHADER_STAGE_FRAGMENT_BIT,0,112};
 VkGraphicsPipelineCreateInfo create[21]={{0}};
 VkPipelineShaderStageCreateInfo stages[21][2]={{{0}}};
 VkPipelineMultisampleStateCreateInfo multisample[21]={{0}};
 VkPipelineRenderingCreateInfo rendering[21]={{0}};
 VkSampleMask masks[21];
 VkVertexInputBindingDescription binding={0};VkVertexInputAttributeDescription attribute={0};
 VkPipelineVertexInputStateCreateInfo vertex={0};VkPipelineInputAssemblyStateCreateInfo assembly={0};
 VkPipelineViewportStateCreateInfo viewport={0};VkPipelineRasterizationStateCreateInfo raster={0};
 VkPipelineColorBlendAttachmentState attachment={0};VkPipelineColorBlendStateCreateInfo blend={0};
 VkDynamicState dynamic_values[2]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};VkPipelineDynamicStateCreateInfo dynamic={0};
 static const unsigned guest_formats[4]={4,5,14,15};
 xe_transfer_address_key key={0};xe_transfer_uint_options options={0};
 unsigned f,msaa,source,sample,count=0,i,j,physical,scale,offset=0;int success=0;
 if(!bank || bank->device || !config || !ops || !scratch || !config->device ||
    !config->image_layout || !config->vertex || config->native2>1 ||
    config->sample_shading>1 || config->dynamic_rendering>1 ||
    !config->scale_x || !config->scale_y || config->scale_x>UINT32_MAX/80 || config->scale_y>UINT32_MAX/16 ||
    !ops->create_shader || !ops->destroy_shader || !ops->create_layout ||
    !ops->destroy_layout || !ops->create_pipelines || !ops->destroy_pipeline)return 0;
 if(!config->dynamic_rendering)for(f=0;f<4;++f)for(msaa=0;msaa<3;++msaa)
  if(!config->passes[f][msaa])return 0;
 bank->device=config->device;bank->native2=config->native2;bank->sample_shading=config->sample_shading;
 bank->scale_classes=config->scale_x==1 && config->scale_y==1?1:4;
 layout.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;layout.setLayoutCount=1;layout.pSetLayouts=&config->image_layout;layout.pushConstantRangeCount=1;layout.pPushConstantRanges=&push;
 /* Single-object failure outputs are undefined, unlike valid partial outputs
  * from graphics-pipeline batches. Publish ownership only after success. */
 if(ops->create_layout(bank->device,&layout,NULL,&layout_result)!=VK_SUCCESS || !layout_result)goto done;
 bank->layout=layout_result;
 binding.stride=16;binding.inputRate=VK_VERTEX_INPUT_RATE_INSTANCE;attribute.format=VK_FORMAT_R32G32B32A32_SFLOAT;
 vertex.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;vertex.vertexBindingDescriptionCount=vertex.vertexAttributeDescriptionCount=1;vertex.pVertexBindingDescriptions=&binding;vertex.pVertexAttributeDescriptions=&attribute;
 assembly.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
 viewport.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;viewport.viewportCount=viewport.scissorCount=1;
 raster.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;raster.polygonMode=VK_POLYGON_MODE_FILL;raster.lineWidth=1;
 attachment.colorWriteMask=15;blend.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;blend.attachmentCount=1;blend.pAttachments=&attachment;
 dynamic.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;dynamic.dynamicStateCount=2;dynamic.pDynamicStates=dynamic_values;
 key.source_color=key.dest_color=1;key.source_pitch=key.dest_pitch=1;
 key.source_native2=key.dest_native2=bank->native2;
 /* Bound temporary creation state to one attachment/scale group. Each driver
  * batch writes directly to its final bank slice; no handle forwarding copy. */
 for(scale=0;scale<bank->scale_classes;++scale)for(f=0;f<4;++f) {
  key.source_scale_x=scale&1?1:config->scale_x;
  key.source_scale_y=scale&1?1:config->scale_y;
  key.dest_scale_x=scale&2?1:config->scale_x;
  key.dest_scale_y=scale&2?1:config->scale_y;key.dest_wide=f&1;
  for(msaa=0;msaa<3;++msaa)for(source=0;source<3;++source) {
   key.dest_msaa=msaa;key.source_msaa=source;
   options.source_multisampled=source!=0;
   options.sample_mode=msaa?(bank->sample_shading?XE_TRANSFER_UINT_SAMPLE_BUILTIN:XE_TRANSFER_UINT_SAMPLE_CONSTANT):XE_TRANSFER_UINT_SINGLE_SAMPLE;
   if(!xe_transfer_uint_spv_build_shape(scratch,&options,&key,guest_formats[f]))goto done;
   module.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;module.codeSize=scratch->count*sizeof(uint32_t);module.pCode=scratch->words;
   XE_DRAW_EVENT(XE_DRAW_UTILITY_PIPELINE,XE_DRAW_MODULE);
   shader_result=VK_NULL_HANDLE;
   if(ops->create_shader(bank->device,&module,NULL,&shader_result)!=VK_SUCCESS || !shader_result)goto done;
   modules[msaa*3+source]=shader_result;
  }
  count=0;
  for(msaa=0;msaa<3;++msaa)for(source=0;source<3;++source)
  for(sample=0;sample<(bank->sample_shading?1:1u<<msaa);++sample) {
   if(count>=21 || offset+count>=XE_TRANSFER_UINT_VK_PIPELINES)goto done;
   physical=msaa==1 && !bank->native2?sample*3:sample;
   masks[count]=msaa==0?1:bank->sample_shading?(msaa==1 && !bank->native2?9:UINT32_MAX):1u<<physical;
   for(j=0;j<2;++j){stages[count][j].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;stages[count][j].pName="main";}
   stages[count][0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[count][0].module=config->vertex;
   stages[count][1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;stages[count][1].module=modules[msaa*3+source];
   multisample[count].sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;multisample[count].rasterizationSamples=(VkSampleCountFlagBits)(msaa==1 && !bank->native2?4:1u<<msaa);multisample[count].sampleShadingEnable=msaa!=0 && bank->sample_shading;multisample[count].minSampleShading=1;multisample[count].pSampleMask=&masks[count];
   create[count].sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;create[count].stageCount=2;create[count].pStages=stages[count];create[count].pVertexInputState=&vertex;create[count].pInputAssemblyState=&assembly;create[count].pViewportState=&viewport;create[count].pRasterizationState=&raster;create[count].pMultisampleState=&multisample[count];create[count].pColorBlendState=&blend;create[count].pDynamicState=&dynamic;create[count].layout=bank->layout;
   if(config->dynamic_rendering) {
    rendering[count].sType=VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;rendering[count].colorAttachmentCount=1;rendering[count].pColorAttachmentFormats=&formats[f];create[count].pNext=&rendering[count];
   } else create[count].renderPass=config->passes[f][msaa];
   ++count;
  }
 /* Driver outputs land in final bank storage, including partial-success
  * handles on an error. Cleanup must visit all slots, not just a success count. */
#if XE_GPU_DRAW_STATS
 for(i=0;i<count;++i)XE_DRAW_EVENT(XE_DRAW_UTILITY_PIPELINE,XE_DRAW_GRAPHICS_PSO);
#endif
 if(ops->create_pipelines(bank->device,VK_NULL_HANDLE,count,create,NULL,bank->pipeline+offset)!=VK_SUCCESS)goto done;
 for(i=0;i<count;++i)if(!bank->pipeline[offset+i])goto done;
 offset+=count;
 for(i=0;i<9;++i)if(modules[i]){ops->destroy_shader(bank->device,modules[i],NULL);modules[i]=VK_NULL_HANDLE;}
 }
 bank->ready=1;success=1;
done:
 for(i=0;i<9;++i)if(modules[i])ops->destroy_shader(bank->device,modules[i],NULL);
 if(!success)xe_transfer_uint_vk_close(bank,ops);
 return success;
}
