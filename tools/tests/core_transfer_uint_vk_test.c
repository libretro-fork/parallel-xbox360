/* Device-free ownership, exact PSO state and lookup checks. BSD; C89. */
#define VK_NO_PROTOTYPES
#include "libretro/core_transfer_uint_vk.h"
#include "libretro/core_draw_stats.h"
#include "libretro/core_transfer_uint_shape.h"
#include <stdio.h>
#include <string.h>
#define HANDLE(type,n) ((type)(uintptr_t)(n))
static struct {
 unsigned calls,fail_at,modules,pipelines,layouts,error,hole_batch,batches,null_at;
 unsigned module_live[144],pipeline_live[336],layout_live;
 xe_transfer_uint_vk_bank *bank;
 const xe_transfer_uint_vk_config *config;
} state;
static int fail(void){return ++state.calls==state.fail_at;}
static VKAPI_ATTR VkResult VKAPI_CALL create_layout(VkDevice device,
 const VkPipelineLayoutCreateInfo *info,const VkAllocationCallbacks *allocator,VkPipelineLayout *out) {
 (void)allocator;
 if(device!=state.config->device || info->setLayoutCount!=1 ||
    info->pSetLayouts[0]!=state.config->image_layout || info->pushConstantRangeCount!=1 ||
    info->pPushConstantRanges[0].size!=112 || info->pPushConstantRanges[0].offset ||
    info->pPushConstantRanges[0].stageFlags!=VK_SHADER_STAGE_FRAGMENT_BIT)state.error=1;
 if(fail()){*out=HANDLE(VkPipelineLayout,0xbad);return VK_ERROR_OUT_OF_HOST_MEMORY;}
 if(state.null_at==state.calls){*out=VK_NULL_HANDLE;return VK_SUCCESS;}
 *out=HANDLE(VkPipelineLayout,50);state.layout_live=1;++state.layouts;return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL destroy_layout(VkDevice device,VkPipelineLayout layout,const VkAllocationCallbacks *allocator) {
 (void)allocator;
 if(device!=state.config->device || layout!=HANDLE(VkPipelineLayout,50) || !state.layout_live)state.error=1;
 state.layout_live=0;
}
static VKAPI_ATTR VkResult VKAPI_CALL create_shader(VkDevice device,
 const VkShaderModuleCreateInfo *info,const VkAllocationCallbacks *allocator,VkShaderModule *out) {
 static const unsigned formats[4]={4,5,14,15};
 xe_transfer_address_key key={0};xe_transfer_uint_options options={0};xe_transfer_uint_shape shape;
 unsigned id=state.modules,group=id/9,scale=group/4,f=group%4,m=(id%9)/3,s=id%3;
 size_t cursor=5,words=info->codeSize/4;uint32_t mask=0;
 (void)allocator;
 if(device!=state.config->device || !info->pCode || info->codeSize<20 || info->pCode[0]!=0x07230203u || id>=144)state.error=1;
 key.source_color=key.dest_color=1;key.source_pitch=key.dest_pitch=1;
 key.source_native2=key.dest_native2=state.config->native2;key.source_msaa=s;key.dest_msaa=m;key.dest_wide=f&1;
 key.source_scale_x=scale&1?1:state.config->scale_x;key.source_scale_y=scale&1?1:state.config->scale_y;
 key.dest_scale_x=scale&2?1:state.config->scale_x;key.dest_scale_y=scale&2?1:state.config->scale_y;
 options.source_multisampled=s!=0;options.sample_mode=m?(state.config->sample_shading?XE_TRANSFER_UINT_SAMPLE_BUILTIN:XE_TRANSFER_UINT_SAMPLE_CONSTANT):XE_TRANSFER_UINT_SINGLE_SAMPLE;
 if(!xe_transfer_uint_shape_prepare(&shape,&options,&key,formats[f]))state.error=1;
 else {
  while(cursor<words) {
   unsigned length=info->pCode[cursor]>>16,op=info->pCode[cursor]&65535;
   if(!length || length>words-cursor){state.error=1;break;}
   if(op==43 && length==4 && info->pCode[cursor+2]>=80 && info->pCode[cursor+2]<108) {
    unsigned word=info->pCode[cursor+2]-80;
    mask|=UINT32_C(1)<<word;
    if(!(shape.mask & (UINT32_C(1)<<word)) || info->pCode[cursor+3]!=xe_transfer_uint_shape_word(&shape,word))state.error=1;
   }
   cursor+=length;
  }
  if(mask!=shape.mask)state.error=1;
 }
 if(fail()){*out=HANDLE(VkShaderModule,0xbad);return VK_ERROR_OUT_OF_HOST_MEMORY;}
 if(state.null_at==state.calls){*out=VK_NULL_HANDLE;return VK_SUCCESS;}
 if(id>=144)return VK_ERROR_OUT_OF_HOST_MEMORY;
 *out=HANDLE(VkShaderModule,100+id);state.module_live[id]=1;++state.modules;return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL destroy_shader(VkDevice device,VkShaderModule module,const VkAllocationCallbacks *allocator) {
 unsigned id=(unsigned)((uintptr_t)module-100);(void)allocator;
 if(device!=state.config->device || id>=144 || !state.module_live[id]){state.error=1;return;}
 state.module_live[id]=0;
}
static VKAPI_ATTR VkResult VKAPI_CALL create_pipelines(VkDevice device,VkPipelineCache cache,
 uint32_t count,const VkGraphicsPipelineCreateInfo *info,const VkAllocationCallbacks *allocator,VkPipeline *out) {
 static const VkFormat formats[4]={VK_FORMAT_R16G16_UINT,VK_FORMAT_R16G16B16A16_UINT,VK_FORMAT_R32_UINT,VK_FORMAT_R32G32_UINT};
 unsigned f,m,s,j,index=0,samples,mask,partial,group=state.batches++,offset;int failed;
 (void)allocator;
 if(device!=state.config->device || cache || count!=(state.config->sample_shading?9u:21u) || out!=state.bank->pipeline+group*(state.config->sample_shading?9u:21u))state.error=1;
 failed=fail();partial=failed?count/2:count;
 f=group%4;offset=group*(state.config->sample_shading?9u:21u);
 for(m=0;m<3;++m)for(s=0;s<3;++s)for(j=0;j<(state.config->sample_shading?1u:1u<<m);++j) {
  const VkGraphicsPipelineCreateInfo *p=info+index;
  const VkPipelineRenderingCreateInfo *r=(const VkPipelineRenderingCreateInfo*)p->pNext;
  samples=m==1 && !state.config->native2?4:1u<<m;
  mask=m==0?1:state.config->sample_shading?(m==1 && !state.config->native2?9:UINT32_MAX):1u<<(m==1 && !state.config->native2?j*3:j);
  if(p->stageCount!=2 || p->pStages[0].module!=state.config->vertex ||
     p->pStages[1].module!=HANDLE(VkShaderModule,100+group*9+m*3+s) ||
     p->layout!=HANDLE(VkPipelineLayout,50) ||
     p->pVertexInputState->pVertexBindingDescriptions[0].stride!=16 ||
     p->pVertexInputState->pVertexBindingDescriptions[0].inputRate!=VK_VERTEX_INPUT_RATE_INSTANCE ||
     p->pMultisampleState->rasterizationSamples!=(VkSampleCountFlagBits)samples ||
     p->pMultisampleState->sampleShadingEnable!=(VkBool32)(m!=0 && state.config->sample_shading) ||
     *p->pMultisampleState->pSampleMask!=mask ||
     p->pColorBlendState->attachmentCount!=1 || p->pColorBlendState->pAttachments[0].colorWriteMask!=15 ||
     p->pColorBlendState->pAttachments[0].blendEnable)state.error=1;
  if(state.config->dynamic_rendering) {
   if(!r || r->sType!=VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO || r->colorAttachmentCount!=1 || *r->pColorAttachmentFormats!=formats[f] || p->renderPass)state.error=1;
  } else if(r || p->renderPass!=state.config->passes[f][m])state.error=1;
  if(index<partial && !(state.hole_batch==group && index==count-1)){out[index]=HANDLE(VkPipeline,200+offset+index);state.pipeline_live[offset+index]=1;++state.pipelines;}
  ++index;
 }
 if(index!=count)state.error=1;
 return failed?VK_ERROR_OUT_OF_DEVICE_MEMORY:VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL destroy_pipeline(VkDevice device,VkPipeline pipeline,const VkAllocationCallbacks *allocator) {
 unsigned id=(unsigned)((uintptr_t)pipeline-200);(void)allocator;
 if(device!=state.config->device || id>=336 || !state.pipeline_live[id]){state.error=1;return;}
 state.pipeline_live[id]=0;
}
static int empty(void) {
 unsigned i;if(state.layout_live)return 0;
 for(i=0;i<144;++i)if(state.module_live[i])return 0;
 for(i=0;i<336;++i)if(state.pipeline_live[i])return 0;
 return !state.error;
}
#define CHECK(x) do {if(!(x)){fprintf(stderr,"Bank test line%u\n",(unsigned)__LINE__);return 1;}}while(0)
int main(void) {
 static const unsigned formats[4]={4,5,14,15};
 xe_transfer_uint_vk_ops ops={create_shader,destroy_shader,create_layout,destroy_layout,create_pipelines,destroy_pipeline},missing;
 xe_transfer_uint_vk_config config={0};xe_transfer_uint_vk_bank bank={0};xe_spv_binary scratch={0};
 unsigned n,shading,dynamic,scaled,fail_at,f,m,s,j,index,contexts=0,groups,max_calls,classes,scale,dn,sn;const VkPipeline *pipelines;uint32_t *borrowed=NULL;
#if XE_GPU_DRAW_STATS
 xe_draw_stats counters;unsigned role,kind,event;
#endif
 config.device=HANDLE(VkDevice,1);config.image_layout=HANDLE(VkDescriptorSetLayout,2);config.vertex=HANDLE(VkShaderModule,3);
 for(f=0;f<4;++f)for(m=0;m<3;++m)config.passes[f][m]=HANDLE(VkRenderPass,400+f*3+m);
 for(n=0;n<2;++n)for(shading=0;shading<2;++shading)for(dynamic=0;dynamic<2;++dynamic)for(scaled=0;scaled<2;++scaled) {
  config.native2=n;config.sample_shading=shading;config.dynamic_rendering=dynamic;
  config.scale_x=scaled?3:1;config.scale_y=scaled?5:1;classes=scaled?4:1;groups=classes*4;max_calls=1+groups*10;
  for(fail_at=0;fail_at<=max_calls+groups+3;++fail_at) {
   memset(&state,0,sizeof(state));state.bank=&bank;state.config=&config;state.fail_at=fail_at<=max_calls?fail_at:0;state.hole_batch=fail_at>max_calls && fail_at<=max_calls+groups?fail_at-max_calls-1:UINT32_MAX;
   state.null_at=fail_at==max_calls+groups+1?1:fail_at==max_calls+groups+2?2:fail_at==max_calls+groups+3?max_calls-1:0;
#if XE_GPU_DRAW_STATS
   xe_draw_stats_reset();xe_draw_stats_enter(XE_DRAW_INITIALIZATION);
#endif
   CHECK(!xe_transfer_uint_vk_get(&bank,4,0,0,0,0));
   if(fail_at) {
    CHECK(!xe_transfer_uint_vk_init(&bank,&config,&ops,&scratch) && empty() && !bank.device && !bank.ready);
   } else {
    CHECK(xe_transfer_uint_vk_init(&bank,&config,&ops,&scratch) && bank.ready && !state.error);
#if XE_GPU_DRAW_STATS
    CHECK(xe_draw_stats_snapshot(&counters,sizeof(counters)));
    CHECK(counters.events[XE_DRAW_INITIALIZATION][XE_DRAW_UTILITY_PIPELINE][XE_DRAW_TRANSLATE]==groups*9);
    CHECK(counters.events[XE_DRAW_INITIALIZATION][XE_DRAW_UTILITY_PIPELINE][XE_DRAW_MODULE]==groups*9);
    CHECK(counters.events[XE_DRAW_INITIALIZATION][XE_DRAW_UTILITY_PIPELINE][XE_DRAW_GRAPHICS_PSO]==groups*(shading?9u:21u));
    CHECK(!xe_draw_stats_violations(&counters));
    xe_draw_stats_reset();xe_draw_stats_enter(XE_DRAW_GPU);
#endif
    CHECK(!xe_transfer_uint_vk_init(&bank,&config,&ops,&scratch));
    if(!borrowed)borrowed=scratch.words;else CHECK(scratch.words==borrowed);
    index=0;
    for(scale=0;scale<classes;++scale)for(f=0;f<4;++f)for(m=0;m<3;++m)for(s=0;s<3;++s) {
     dn=(scale>>1)&1;sn=scale&1;
     pipelines=xe_transfer_uint_vk_get(&bank,formats[f],m,s,dn,sn);CHECK(pipelines);
     if(!scaled)for(dn=0;dn<2;++dn)for(sn=0;sn<2;++sn)CHECK(pipelines==xe_transfer_uint_vk_get(&bank,formats[f],m,s,dn,sn));
     for(j=0;j<(shading?1u:1u<<m);++j){CHECK(pipelines[j]==HANDLE(VkPipeline,200+index));++index;}
     for(j=0;j<(1u<<m);++j)CHECK(xe_transfer_uint_vk_physical_sample(&bank,m,j)==(m==1 && !n?j*3:j));
    }
    CHECK(index==groups*(shading?9u:21u));CHECK(!xe_transfer_uint_vk_get(&bank,8,0,0,0,0) && !xe_transfer_uint_vk_get(&bank,4,3,0,0,0) && !xe_transfer_uint_vk_get(&bank,4,0,3,0,0));
    CHECK(xe_transfer_uint_vk_physical_sample(&bank,1,2)==UINT32_MAX);
#if XE_GPU_DRAW_STATS
    CHECK(xe_draw_stats_snapshot(&counters,sizeof(counters)));
    for(role=0;role<XE_DRAW_ROLE_COUNT;++role)for(kind=0;kind<XE_DRAW_KIND_COUNT;++kind)
     for(event=0;event<XE_DRAW_EVENT_COUNT;++event)CHECK(!counters.events[role][kind][event]);
#endif
    missing=ops;missing.destroy_pipeline=NULL;CHECK(!xe_transfer_uint_vk_close(&bank,&missing) && bank.ready);
    CHECK(xe_transfer_uint_vk_close(&bank,&ops) && empty() && !bank.ready);
   }
   CHECK(xe_transfer_uint_vk_close(&bank,&ops) && empty());++contexts;
  }
 }
 xe_spv_binary_close(&scratch);printf("PASS: %u bank contexts, exact36/84 native and144/336 scaled PSO states, lookup-only identity, partial driver failure cleanup\n",contexts);return 0;
}
