/* Initialization-only ownership; final fixed PSO storage. BSD; MSVC C89. */
#define VK_NO_PROTOTYPES
#include "core_transfer_color_vk.h"
#include "core_draw_stats.h"
#include <string.h>
static int color_class(unsigned format,unsigned *out) {
 switch(format){
  case 0:case 1:case 2:case 3:*out=format;return 1;
  case 10:*out=2;return 1;case 12:*out=3;return 1;
  case 4:case 6:*out=4;return 1;case 5:case 7:*out=5;return 1;
  case 14:*out=6;return 1;case 15:*out=7;return 1;
  default:return 0;
 }
}
static int color_pair(unsigned s,unsigned d,unsigned *index){
 if((s>=4 && d>=4) || s==d)return 0;
 *index=s<4?s*7+d-(d>s?1u:0u):28+(s-4)*4+d;return 1;
}
const VkPipeline *xe_transfer_color_vk_get(const xe_transfer_color_vk_bank *bank,
 unsigned source_format,unsigned dest_format,unsigned dest_native,unsigned source_native) {
 unsigned s,d,scale,index;
 if(!bank || !bank->ready || !color_class(source_format,&s) ||
    !color_class(dest_format,&d) || !(bank->enabled_views&(1u<<s)) ||
    !(bank->enabled_views&(1u<<d)) || dest_native>1 || source_native>1 || !color_pair(s,d,&index))return NULL;
 scale=bank->scale_classes==1?0:dest_native*2+source_native;
 index+=scale*XE_TRANSFER_COLOR_VK_PAIRS;
 return bank->pipeline[index]?bank->pipeline+index:NULL;
}
static void color_shape(const xe_transfer_color_vk_config *config,unsigned scale,unsigned s,unsigned d,
 xe_transfer_address_key *key,xe_transfer_color_options *options){
 memset(key,0,sizeof(*key));memset(options,0,sizeof(*options));
 key->source_color=key->dest_color=1;key->source_pitch=key->dest_pitch=1;
 key->source_native2=key->dest_native2=config->address.native2;
 key->source_scale_x=scale&1?1:config->address.scale_x;key->source_scale_y=scale&1?1:config->address.scale_y;
 key->dest_scale_x=scale&2?1:config->address.scale_x;key->dest_scale_y=scale&2?1:config->address.scale_y;
 key->source_wide=s==5 || s==7;key->dest_wide=d==5 || d==7;
 options->source_uint=s>=4;options->dest_uint=d>=4;
}
int xe_transfer_color_vk_close(xe_transfer_color_vk_bank *bank,
 const xe_transfer_color_vk_ops *ops) {
 unsigned i;
 if(!bank)return 0;
 if(bank->device){
  if(!ops || !ops->destroy_pipeline || !ops->destroy_layout)return 0;
  for(i=0;i<XE_TRANSFER_COLOR_VK_PIPELINES;++i)if(bank->pipeline[i])
   ops->destroy_pipeline(bank->device,bank->pipeline[i],NULL);
  if(bank->layout)ops->destroy_layout(bank->device,bank->layout,NULL);
 }
 memset(bank,0,sizeof(*bank));return 1;
}
int xe_transfer_color_vk_init(xe_transfer_color_vk_bank *bank,
 const xe_transfer_color_vk_config *config,const xe_transfer_color_vk_ops *ops,
 xe_spv_binary *scratch) {
 static const VkFormat formats[8]={VK_FORMAT_R8G8B8A8_UNORM,
  VK_FORMAT_R16G16B16A16_UNORM,VK_FORMAT_A2B10G10R10_UNORM_PACK32,
  VK_FORMAT_R16G16B16A16_SFLOAT,VK_FORMAT_R16G16_UINT,
  VK_FORMAT_R16G16B16A16_UINT,VK_FORMAT_R32_UINT,VK_FORMAT_R32G32_UINT};
 static const unsigned guest_formats[8]={0,1,2,3,4,5,14,15};
 VkShaderModule shader=VK_NULL_HANDLE,shader_result=VK_NULL_HANDLE;
 VkPipelineLayout layout_result=VK_NULL_HANDLE;
 VkShaderModuleCreateInfo module={0};VkPipelineLayoutCreateInfo layout={0};
 VkPushConstantRange push={VK_SHADER_STAGE_FRAGMENT_BIT,0,32};
 VkGraphicsPipelineCreateInfo create={0};VkPipelineShaderStageCreateInfo stages[2]={{0}};
 VkPipelineRenderingCreateInfo rendering={0};VkPipelineMultisampleStateCreateInfo multisample={0};
 VkVertexInputBindingDescription binding={0};VkVertexInputAttributeDescription attribute={0};
 VkPipelineVertexInputStateCreateInfo vertex={0};VkPipelineInputAssemblyStateCreateInfo assembly={0};
 VkPipelineViewportStateCreateInfo viewport={0};VkPipelineRasterizationStateCreateInfo raster={0};
 VkPipelineColorBlendAttachmentState attachment={0};VkPipelineColorBlendStateCreateInfo blend={0};
 VkDynamicState dynamic_values[2]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
 VkPipelineDynamicStateCreateInfo dynamic={0};xe_transfer_color_options options={0};
 xe_transfer_address_key key={0};size_t words,maximum=0;
 unsigned scale,s,d,index;int success=0;
 if(!bank || bank->device || !config || !ops || !scratch || !config->device ||
    !config->image_layout || !config->vertex || !config->enabled_views ||
    config->enabled_views>255 || config->dynamic_rendering>1 ||
    !xe_transfer_address_packet_config_valid(&config->address) ||
    !ops->create_shader || !ops->destroy_shader || !ops->create_layout ||
    !ops->destroy_layout || !ops->create_pipelines || !ops->destroy_pipeline)return 0;
 for(d=0;d<8;++d)if(config->enabled_views&(1u<<d)){
  if(!config->dynamic_rendering && !config->passes[d])return 0;
 }
 bank->device=config->device;bank->enabled_views=config->enabled_views;
 bank->packed_address=config->emit_packed!=NULL;
 if(bank->packed_address)push.size=4;
 bank->scale_classes=config->address.scale_x==1 && config->address.scale_y==1?1:4;
 layout.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;layout.setLayoutCount=1;
 layout.pSetLayouts=&config->image_layout;layout.pushConstantRangeCount=1;layout.pPushConstantRanges=&push;
 if(ops->create_layout(bank->device,&layout,NULL,&layout_result)!=VK_SUCCESS || !layout_result)goto done;
 bank->layout=layout_result;
 /* Plan metadata only, then reserve the largest final module exactly once.
  * No intermediate word streams and no grow-and-copy chain. */
 if(!config->emit_packed){
 for(scale=0;scale<bank->scale_classes;++scale)for(s=0;s<8;++s)if(config->enabled_views&(1u<<s))
 for(d=0;d<8;++d)if(config->enabled_views&(1u<<d)){
  if(!color_pair(s,d,&index))continue;
  color_shape(config,scale,s,d,&key,&options);
  if(!xe_transfer_color_spv_shape_words(&words,&options,&config->address,&key,guest_formats[s],guest_formats[d]))goto done;
  if(words>maximum)maximum=words;
 }
 xe_spv_binary_reset(scratch);if(!xe_spv_binary_reserve_exact(scratch,maximum))goto done;
 }
 binding.stride=16;binding.inputRate=VK_VERTEX_INPUT_RATE_INSTANCE;attribute.format=VK_FORMAT_R32G32B32A32_SFLOAT;
 vertex.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;vertex.vertexBindingDescriptionCount=vertex.vertexAttributeDescriptionCount=1;
 vertex.pVertexBindingDescriptions=&binding;vertex.pVertexAttributeDescriptions=&attribute;
 assembly.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
 viewport.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;viewport.viewportCount=viewport.scissorCount=1;
 raster.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;raster.polygonMode=VK_POLYGON_MODE_FILL;raster.lineWidth=1;
 multisample.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;multisample.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
 attachment.colorWriteMask=15;blend.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;blend.attachmentCount=1;blend.pAttachments=&attachment;
 dynamic.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;dynamic.dynamicStateCount=2;dynamic.pDynamicStates=dynamic_values;
 stages[0].sType=stages[1].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
 stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[0].module=config->vertex;stages[0].pName=stages[1].pName="main";
 stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;
 create.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;create.stageCount=2;create.pStages=stages;
 create.pVertexInputState=&vertex;create.pInputAssemblyState=&assembly;create.pViewportState=&viewport;
 create.pRasterizationState=&raster;create.pMultisampleState=&multisample;create.pColorBlendState=&blend;
 create.pDynamicState=&dynamic;create.layout=bank->layout;
 rendering.sType=VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;rendering.colorAttachmentCount=1;
 if(config->dynamic_rendering)create.pNext=&rendering;
 for(scale=0;scale<bank->scale_classes;++scale)for(s=0;s<8;++s)if(config->enabled_views&(1u<<s))
 for(d=0;d<8;++d)if(config->enabled_views&(1u<<d)){
  if(!color_pair(s,d,&index))continue;
  color_shape(config,scale,s,d,&key,&options);
  if(config->emit_packed){
   if(!config->emit_packed(config->emit_context,guest_formats[s],guest_formats[d],
      scale>>1,scale&1,scratch) || !scratch->count || !scratch->words ||
      scratch->failed || scratch->count>scratch->capacity ||
      scratch->count>SIZE_MAX/sizeof(uint32_t))goto done;
  }else if(!xe_transfer_color_spv_build_shape(scratch,&options,&config->address,&key,guest_formats[s],guest_formats[d]))goto done;
  module.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;module.codeSize=scratch->count*sizeof(uint32_t);module.pCode=scratch->words;
  XE_DRAW_EVENT(XE_DRAW_UTILITY_PIPELINE,XE_DRAW_MODULE);shader_result=VK_NULL_HANDLE;
  if(ops->create_shader(bank->device,&module,NULL,&shader_result)!=VK_SUCCESS || !shader_result)goto done;
  shader=shader_result;stages[1].module=shader;
  if(config->dynamic_rendering)rendering.pColorAttachmentFormats=formats+d;
  else create.renderPass=config->passes[d];
  XE_DRAW_EVENT(XE_DRAW_UTILITY_PIPELINE,XE_DRAW_GRAPHICS_PSO);
  /* Vulkan defines partial-success pipeline outputs on errors. Write directly
   * to the owned slot so failure cleanup cannot lose a live driver object. */
  if(ops->create_pipelines(bank->device,VK_NULL_HANDLE,1,&create,NULL,
     bank->pipeline+scale*XE_TRANSFER_COLOR_VK_PAIRS+index)!=VK_SUCCESS ||
     !bank->pipeline[scale*XE_TRANSFER_COLOR_VK_PAIRS+index])goto done;
  ops->destroy_shader(bank->device,shader,NULL);shader=VK_NULL_HANDLE;
 }
 bank->ready=1;success=1;
done:
 if(shader)ops->destroy_shader(bank->device,shader,NULL);
 if(!success)xe_transfer_color_vk_close(bank,ops);
 return success;
}
