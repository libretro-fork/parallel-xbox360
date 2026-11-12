/* Existing numeric producer's exact typed PSOs. Test-only; BSD; MSVC C89.
 * No substitute shader arithmetic or integer-view rendering is introduced. */
static struct {
 VkPipelineLayout layout;VkShaderModule module;
 VkPipeline pipeline[2][4][3][3][2][4];
} identity_legacy;
static void identity_legacy_close(void)
{
 unsigned n,v,m,s,k,p;
 for(n=0;n<2;++n)for(v=0;v<4;++v)for(m=0;m<3;++m)for(s=0;s<3;++s)
 for(k=0;k<2;++k)for(p=0;p<4;++p)if(identity_legacy.pipeline[n][v][m][s][k][p])
  g.vkDestroyPipeline(g.device,identity_legacy.pipeline[n][v][m][s][k][p],NULL);
 if(identity_legacy.module)g.vkDestroyShaderModule(g.device,identity_legacy.module,NULL);
 if(identity_legacy.layout)g.vkDestroyPipelineLayout(g.device,identity_legacy.layout,NULL);
 memset(&identity_legacy,0,sizeof(identity_legacy));
}
static int identity_legacy_init(const bank *b,VkDescriptorSetLayout images,
 const char *prefix,unsigned native2,unsigned shading,unsigned dynamic_rendering)
{
 VkPipelineLayoutCreateInfo layout={0};VkPushConstantRange push={VK_SHADER_STAGE_FRAGMENT_BIT,0,4};
 VkPipelineShaderStageCreateInfo stages[2]={{0},{0}};VkVertexInputBindingDescription binding={0};VkVertexInputAttributeDescription attribute={0};
 VkPipelineVertexInputStateCreateInfo vertex={0};VkPipelineInputAssemblyStateCreateInfo assembly={0};
 VkPipelineViewportStateCreateInfo viewport={0};VkPipelineRasterizationStateCreateInfo raster={0};
 VkPipelineMultisampleStateCreateInfo msaa={0};VkPipelineColorBlendAttachmentState attachment={0};VkPipelineColorBlendStateCreateInfo blend={0};
 VkDynamicState values[2]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};VkPipelineDynamicStateCreateInfo dynamic={0};
 VkGraphicsPipelineCreateInfo pipeline={0};VkPipelineRenderingCreateInfo rendering={0};VkFormat format;
 VkSpecializationMapEntry map={0,0,4};VkSpecializationInfo spec={0};
 VkSampleMask mask;unsigned n,v,m,s,k,p,physical,index;char path[4096];
 if(!prefix || strlen(prefix)>3000 || native2>1 || shading>1 || dynamic_rendering>1)return 0;
 spec.mapEntryCount=1;spec.pMapEntries=&map;spec.dataSize=4;
 layout.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;layout.setLayoutCount=1;layout.pSetLayouts=&images;
 layout.pushConstantRangeCount=1;layout.pPushConstantRanges=&push;
 if(g.vkCreatePipelineLayout(g.device,&layout,NULL,&identity_legacy.layout)!=VK_SUCCESS)return 0;
 stages[0].sType=stages[1].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
 stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[0].module=b->vertex;stages[0].pName=stages[1].pName="main";
 stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;
 binding.stride=16;binding.inputRate=VK_VERTEX_INPUT_RATE_INSTANCE;attribute.format=VK_FORMAT_R32G32B32A32_SFLOAT;
 vertex.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;vertex.vertexBindingDescriptionCount=vertex.vertexAttributeDescriptionCount=1;vertex.pVertexBindingDescriptions=&binding;vertex.pVertexAttributeDescriptions=&attribute;
 assembly.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
 viewport.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;viewport.viewportCount=viewport.scissorCount=1;
 raster.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;raster.polygonMode=VK_POLYGON_MODE_FILL;raster.lineWidth=1;
 msaa.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;msaa.pSampleMask=&mask;msaa.minSampleShading=1;
 attachment.colorWriteMask=15;blend.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;blend.attachmentCount=1;blend.pAttachments=&attachment;
 dynamic.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;dynamic.dynamicStateCount=2;dynamic.pDynamicStates=values;
 pipeline.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;pipeline.stageCount=2;pipeline.pStages=stages;
 pipeline.pVertexInputState=&vertex;pipeline.pInputAssemblyState=&assembly;pipeline.pViewportState=&viewport;
 pipeline.pRasterizationState=&raster;pipeline.pMultisampleState=&msaa;pipeline.pColorBlendState=&blend;
 pipeline.pDynamicState=&dynamic;pipeline.layout=identity_legacy.layout;
 rendering.sType=VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;rendering.colorAttachmentCount=1;rendering.pColorAttachmentFormats=&format;
 for(n=0;n<(native2?2u:1u);++n)for(v=0;v<4;++v)for(m=0;m<3;++m)for(s=0;s<3;++s)
 for(k=0;k<(m?2u:1u);++k) {
  if(m && !shading && !k)continue;
  index=m==1 && !n?2:m;format=identity_ms_numeric_format(v);
  pipeline.pNext=dynamic_rendering?&rendering:NULL;
  pipeline.renderPass=dynamic_rendering?VK_NULL_HANDLE:b->numeric_render[v][index];
  msaa.rasterizationSamples=(VkSampleCountFlagBits)(1u<<index);msaa.sampleShadingEnable=m && !k;
  sprintf(path,"%s-n%u-v%u-m%u-s%u-mode%u.spv",prefix,n,v,m,1u<<s,k);
  if(!shader(path,&identity_legacy.module))return 0;
  stages[1].module=identity_legacy.module;stages[1].pSpecializationInfo=k?&spec:NULL;
  for(p=0;p<(k?(1u<<m):1u);++p) {
   physical=m==1 && !n?p*3:p;spec.pData=&physical;
   mask=k?1u<<physical:m==1 && !n?9u:(1u<<(1u<<m))-1;
   if(g.vkCreateGraphicsPipelines(g.device,VK_NULL_HANDLE,1,&pipeline,NULL,
    &identity_legacy.pipeline[n][v][m][s][k][p])!=VK_SUCCESS)return 0;
  }
  g.vkDestroyShaderModule(g.device,identity_legacy.module,NULL);identity_legacy.module=VK_NULL_HANDLE;
 }
 return 1;
}
