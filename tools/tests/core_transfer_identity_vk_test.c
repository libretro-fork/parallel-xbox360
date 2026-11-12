/* Actual shader emission, driver recording and every acquisition failure. BSD; C89. */
#define VK_NO_PROTOTYPES
#include "libretro/core_transfer_identity_vk.h"
#include "libretro/core_draw_stats.h"
#include <spirv/unified1/spirv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define HANDLE(type,n) ((type)(uintptr_t)(n))
#define CHECK(x) do {if(!(x)){fprintf(stderr,"identity vk line%d\n",__LINE__);exit(1);}}while(0)
typedef struct recorded_module {unsigned live,scale,dest,source;} recorded_module;
static struct {
 unsigned calls,fail_at,null_at,modules,pipelines,layout_live;
 unsigned live[336];recorded_module module[36];
 xe_transfer_identity_vk_bank *bank;
 const xe_transfer_identity_vk_config *config;
} state;
static int fail(void){return ++state.calls==state.fail_at;}
static VKAPI_ATTR VkResult VKAPI_CALL create_layout(VkDevice device,const VkPipelineLayoutCreateInfo *info,
 const VkAllocationCallbacks *allocator,VkPipelineLayout *out)
{
 (void)allocator;CHECK(device==state.config->device && info->setLayoutCount==1 &&
  info->pSetLayouts[0]==state.config->image_layout && info->pushConstantRangeCount==1 &&
  info->pPushConstantRanges[0].size==20 && !info->pPushConstantRanges[0].offset &&
  info->pPushConstantRanges[0].stageFlags==VK_SHADER_STAGE_FRAGMENT_BIT);
 if(fail()){*out=HANDLE(VkPipelineLayout,0xbad);return VK_ERROR_OUT_OF_HOST_MEMORY;}
 if(state.calls==state.null_at)return VK_SUCCESS;
 *out=HANDLE(VkPipelineLayout,50);state.layout_live=1;return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL destroy_layout(VkDevice device,VkPipelineLayout layout,const VkAllocationCallbacks *allocator)
{
 unsigned i;(void)allocator;
 CHECK(device==state.config->device && layout==HANDLE(VkPipelineLayout,50) && state.layout_live);
 for(i=0;i<336;++i)CHECK(!state.live[i]);
 state.layout_live=0;
}
static VKAPI_ATTR VkResult VKAPI_CALL create_shader(VkDevice device,const VkShaderModuleCreateInfo *info,
 const VkAllocationCallbacks *allocator,VkShaderModule *out)
{
 uint32_t fixed[XE_ADDR_WORDS]={0},mask=0,color=0;size_t cursor=5,words=info->codeSize/4;
 unsigned op,length,id,fetches=0,stores=0,scale;recorded_module *record;
 (void)allocator;CHECK(device==state.config->device && info->pCode && words>5 && info->pCode[0]==0x07230203u);
 while(cursor<words) {
  length=info->pCode[cursor]>>16;op=info->pCode[cursor]&65535;
  CHECK(length && length<=words-cursor);
  if(op==SpvOpConstant && length==4 && info->pCode[cursor+2]>=80 && info->pCode[cursor+2]<80+XE_ADDR_WORDS) {
   id=info->pCode[cursor+2]-80;mask|=UINT32_C(1)<<id;fixed[id]=info->pCode[cursor+3];
  }
  if(op==SpvOpImageFetch){CHECK(length==7);color=info->pCode[cursor+2];++fetches;}
  if(op==SpvOpStore){CHECK(length==3 && info->pCode[cursor+2]==color);++stores;}
  CHECK(op!=SpvOpFAdd && op!=SpvOpFSub && op!=SpvOpFMul && op!=SpvOpFDiv);
  cursor+=length;
 }
 CHECK(fetches==1 && stores==1 && mask==UINT32_C(0x43ffff));
 CHECK(!fixed[XE_ADDR_D_WIDE] && !fixed[XE_ADDR_S_WIDE] && !fixed[XE_ADDR_SWAP]);
 CHECK(fixed[XE_ADDR_D_NATIVE2]==state.config->native2 && fixed[XE_ADDR_S_NATIVE2]==state.config->native2);
 CHECK(fixed[XE_ADDR_D_MSAA]<3 && fixed[XE_ADDR_S_MSAA]<3);
 scale=state.config->scale_x==1?0:(fixed[XE_ADDR_D_SX]==1?2u:0u)|(fixed[XE_ADDR_S_SX]==1?1u:0u);
 CHECK(fixed[XE_ADDR_D_SX]==(scale&2?1:state.config->scale_x));
 CHECK(fixed[XE_ADDR_D_SY]==(scale&2?1:state.config->scale_y));
 CHECK(fixed[XE_ADDR_S_SX]==(scale&1?1:state.config->scale_x));
 CHECK(fixed[XE_ADDR_S_SY]==(scale&1?1:state.config->scale_y));
 if(fail()){*out=HANDLE(VkShaderModule,0xbad);return VK_ERROR_OUT_OF_HOST_MEMORY;}
 if(state.calls==state.null_at)return VK_SUCCESS;
 id=state.modules++;CHECK(id<36);record=&state.module[id];
 record->live=1;record->scale=scale;record->dest=fixed[XE_ADDR_D_MSAA];record->source=fixed[XE_ADDR_S_MSAA];
 *out=HANDLE(VkShaderModule,100+id);return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL destroy_shader(VkDevice device,VkShaderModule module,const VkAllocationCallbacks *allocator)
{
 unsigned id=(unsigned)((uintptr_t)module-100);(void)allocator;
 CHECK(device==state.config->device && id<36 && state.module[id].live);state.module[id].live=0;
}
static VKAPI_ATTR VkResult VKAPI_CALL create_pipelines(VkDevice device,VkPipelineCache cache,uint32_t count,
 const VkGraphicsPipelineCreateInfo *info,const VkAllocationCallbacks *allocator,VkPipeline *out)
{
 static const VkFormat views[4]={VK_FORMAT_R8G8B8A8_UNORM,VK_FORMAT_R16G16B16A16_UNORM,
  VK_FORMAT_A2B10G10R10_UNORM_PACK32,VK_FORMAT_R16G16B16A16_SFLOAT};
 const VkPipelineRenderingCreateInfo *rendering=(const VkPipelineRenderingCreateInfo*)info[0].pNext;
 unsigned module=(unsigned)((uintptr_t)info[0].pStages[1].module-100),f,j,group,offset,physical,coverage,partial;
 const recorded_module *record;int failed;
 (void)allocator;CHECK(device==state.config->device && !cache && module<36);record=&state.module[module];CHECK(record->live);
 if(state.config->dynamic_rendering) {
  CHECK(rendering && rendering->colorAttachmentCount==1 && !info[0].renderPass);
  for(f=0;f<4 && *rendering->pColorAttachmentFormats!=views[f];++f){}
 } else {
  CHECK(!rendering);for(f=0;f<4 && info[0].renderPass!=state.config->passes[f][record->dest];++f){}
 }
 CHECK(f<4 && (state.config->sample_mask[f]&(1u<<record->dest)) && (state.config->sample_mask[f]&(1u<<record->source)));
 group=record->scale*4+f;
 offset=state.config->sample_shading?(group*3+record->dest)*3+record->source:
  group*21+((1u<<record->dest)-1)*3+record->source*(1u<<record->dest);
 CHECK(out==state.bank->pipeline+offset && count==(state.config->sample_shading?1u:1u<<record->dest));
 failed=fail();partial=failed?count/2:count;
 if(state.calls==state.null_at)partial=count-1;
 for(j=0;j<count;++j) {
  const VkGraphicsPipelineCreateInfo *p=info+j;
  physical=record->dest==1 && !state.config->native2?j*3:j;
  coverage=!record->dest?1:state.config->sample_shading?
   (record->dest==1 && !state.config->native2?9:UINT32_MAX):1u<<physical;
  CHECK(p->pStages[0].module==state.config->vertex && p->pStages[1].module==info[0].pStages[1].module &&
   p->layout==HANDLE(VkPipelineLayout,50) && p->stageCount==2 && p->pVertexInputState->vertexBindingDescriptionCount==1 &&
   p->pVertexInputState->pVertexBindingDescriptions[0].stride==16 &&
   p->pVertexInputState->pVertexBindingDescriptions[0].inputRate==VK_VERTEX_INPUT_RATE_INSTANCE);
  CHECK(p->pMultisampleState->rasterizationSamples==(VkSampleCountFlagBits)(record->dest==1 && !state.config->native2?4:1u<<record->dest) &&
   p->pMultisampleState->sampleShadingEnable==(VkBool32)(record->dest!=0 && state.config->sample_shading) &&
   *p->pMultisampleState->pSampleMask==coverage && !p->pColorBlendState->pAttachments[0].blendEnable &&
   p->pColorBlendState->pAttachments[0].colorWriteMask==15);
  if(j<partial){CHECK(!state.live[offset+j]);state.live[offset+j]=1;out[j]=HANDLE(VkPipeline,200+offset+j);++state.pipelines;}
 }
 return failed?VK_ERROR_OUT_OF_DEVICE_MEMORY:VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL destroy_pipeline(VkDevice device,VkPipeline pipeline,const VkAllocationCallbacks *allocator)
{
 unsigned slot=(unsigned)((uintptr_t)pipeline-200);(void)allocator;
 CHECK(device==state.config->device && state.layout_live && slot<336 && state.live[slot]);state.live[slot]=0;
}
static void empty(void)
{
 unsigned i;CHECK(!state.layout_live);
 for(i=0;i<36;++i)CHECK(!state.module[i].live);
 for(i=0;i<336;++i)CHECK(!state.live[i]);
}
static const xe_transfer_identity_vk_ops ops={create_shader,destroy_shader,create_layout,destroy_layout,create_pipelines,destroy_pipeline};
int main(void)
{
 xe_transfer_identity_vk_config config={0};xe_transfer_identity_vk_bank bank={0},snapshot;
 xe_transfer_identity_vk_ops missing=ops;xe_spv_binary scratch={0};
 unsigned n,shading,dynamic,scaled,caps,i,f,d,s,scale,count,max_calls,trial,runs=0;
 uint32_t *borrowed=NULL;const VkPipeline *found;xe_draw_stats counters;
 config.device=HANDLE(VkDevice,1);config.image_layout=HANDLE(VkDescriptorSetLayout,2);config.vertex=HANDLE(VkShaderModule,3);
 for(f=0;f<4;++f)for(d=0;d<3;++d)config.passes[f][d]=HANDLE(VkRenderPass,400+f*3+d);
 for(n=0;n<2;++n)for(shading=0;shading<2;++shading)for(dynamic=0;dynamic<2;++dynamic)
 for(scaled=0;scaled<2;++scaled)for(caps=0;caps<3;++caps) {
  config.native2=n;config.sample_shading=shading;config.dynamic_rendering=dynamic;
  config.scale_x=scaled?3:1;config.scale_y=scaled?5:1;
  for(f=0;f<4;++f)config.sample_mask[f]=caps==2?0:caps?(f==0?7u:f==1?0u:f==2?5u:1u):7u;
  max_calls=0;
  for(trial=0;trial<=max_calls*2;++trial) {
   memset(&state,0,sizeof(state));state.config=&config;state.bank=&bank;
   state.fail_at=trial<=max_calls?trial:0;state.null_at=trial>max_calls?trial-max_calls:0;
   xe_draw_stats_reset();xe_draw_stats_enter(XE_DRAW_INITIALIZATION);
   if(trial)CHECK(!xe_transfer_identity_vk_init(&bank,&config,&ops,&scratch));
   else {
    CHECK(xe_transfer_identity_vk_init(&bank,&config,&ops,&scratch));max_calls=state.calls;
    CHECK(xe_draw_stats_snapshot(&counters,sizeof(counters)) && !xe_draw_stats_violations(&counters));
    CHECK(counters.events[XE_DRAW_INITIALIZATION][XE_DRAW_UTILITY_PIPELINE][XE_DRAW_TRANSLATE]==state.modules &&
     counters.events[XE_DRAW_INITIALIZATION][XE_DRAW_UTILITY_PIPELINE][XE_DRAW_MODULE]==state.modules &&
     counters.events[XE_DRAW_INITIALIZATION][XE_DRAW_UTILITY_PIPELINE][XE_DRAW_GRAPHICS_PSO]==state.pipelines);
    xe_draw_stats_reset();xe_draw_stats_enter(XE_DRAW_GPU);
    for(scale=0;scale<4;++scale)for(f=0;f<4;++f)for(d=0;d<3;++d)for(s=0;s<3;++s) {
     found=xe_transfer_identity_vk_get(&bank,f,d,s,scale>>1,scale&1);
     if(!(config.sample_mask[f]&(1u<<d)) || !(config.sample_mask[f]&(1u<<s)))CHECK(!found);
     else {
      CHECK(found);count=shading?1:1u<<d;
      for(i=0;i<count;++i) {
       CHECK((uintptr_t)found[i]>=200 && (uintptr_t)found[i]<536);
       CHECK(state.live[(uintptr_t)found[i]-200]);
      }
     }
    }
    CHECK(xe_draw_stats_snapshot(&counters,sizeof(counters)) && !xe_draw_stats_violations(&counters));
    for(i=0;i<XE_DRAW_EVENT_COUNT;++i)CHECK(!counters.events[XE_DRAW_GPU][XE_DRAW_UTILITY_PIPELINE][i]);
    snapshot=bank;missing.destroy_pipeline=NULL;
    CHECK(!xe_transfer_identity_vk_close(&bank,&missing) && !memcmp(&snapshot,&bank,sizeof(bank)));
    CHECK(!xe_transfer_identity_vk_init(&bank,&config,&ops,&scratch));
   }
   if(scratch.words){if(borrowed)CHECK(scratch.words==borrowed);else borrowed=scratch.words;}
   CHECK(xe_transfer_identity_vk_close(&bank,&ops));empty();
   CHECK(!bank.device && !bank.ready);++runs;
  }
 }
 xe_spv_binary_close(&scratch);
 printf("PASS: %u Vulkan identity bank contexts, typed PSOs, shared modules, sparse caps, partial/null failures and zero draw-role events\n",runs);return 0;
}
