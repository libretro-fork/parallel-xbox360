/* Device-free failure, ownership and immutable lookup proof. BSD; C89. */
#define VK_NO_PROTOTYPES
#include "libretro/core_transfer_color_vk.h"
#include <stdio.h>
#include <string.h>
#define H(t,n) ((t)(uintptr_t)(n))
static struct {
 unsigned emit_calls,emit_fail,emit_empty,emit_bad,calls,fail_at,null_at,modules,pipelines,layout,error,shader_live[176],pipeline_live[176];
 xe_transfer_color_vk_bank *bank;const xe_transfer_color_vk_config *config;
} state;
static int fail(void){return ++state.calls==state.fail_at;}
static VKAPI_ATTR VkResult VKAPI_CALL create_layout(VkDevice device,
 const VkPipelineLayoutCreateInfo *p,const VkAllocationCallbacks *a,VkPipelineLayout *out){
 (void)a;
 if(device!=state.config->device || p->setLayoutCount!=1 ||
    p->pSetLayouts[0]!=state.config->image_layout || p->pushConstantRangeCount!=1 ||
    p->pPushConstantRanges[0].size!=(state.config->emit_packed?4u:32u) || p->pPushConstantRanges[0].offset ||
    p->pPushConstantRanges[0].stageFlags!=VK_SHADER_STAGE_FRAGMENT_BIT)state.error=1;
 if(fail()){*out=H(VkPipelineLayout,0xbad);return VK_ERROR_OUT_OF_HOST_MEMORY;}
 if(state.calls==state.null_at){*out=VK_NULL_HANDLE;return VK_SUCCESS;}
 state.layout=1;*out=H(VkPipelineLayout,50);return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL destroy_layout(VkDevice d,VkPipelineLayout p,const VkAllocationCallbacks *a){
 (void)a;if(d!=state.config->device || p!=H(VkPipelineLayout,50) || !state.layout)state.error=1;state.layout=0;
}
static VKAPI_ATTR VkResult VKAPI_CALL create_shader(VkDevice d,
 const VkShaderModuleCreateInfo *p,const VkAllocationCallbacks *a,VkShaderModule *out){
 unsigned i=state.modules;(void)a;
 if(d!=state.config->device || !p->pCode || p->codeSize<20 || p->pCode[0]!=0x07230203u || i>=176)state.error=1;
 if(fail()){*out=H(VkShaderModule,0xbad);return VK_ERROR_OUT_OF_HOST_MEMORY;}
 if(state.calls==state.null_at){*out=VK_NULL_HANDLE;return VK_SUCCESS;}
 if(i>=176)return VK_ERROR_OUT_OF_HOST_MEMORY;
 state.shader_live[i]=1;++state.modules;*out=H(VkShaderModule,100+i);return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL destroy_shader(VkDevice d,VkShaderModule p,const VkAllocationCallbacks *a){
 unsigned i=(unsigned)((uintptr_t)p-100);(void)a;
 if(d!=state.config->device || i>=176 || !state.shader_live[i]){state.error=1;return;}state.shader_live[i]=0;
}
static VKAPI_ATTR VkResult VKAPI_CALL create_pipelines(VkDevice d,VkPipelineCache cache,
 uint32_t n,const VkGraphicsPipelineCreateInfo *p,const VkAllocationCallbacks *a,VkPipeline *out){
 static const VkFormat formats[8]={VK_FORMAT_R8G8B8A8_UNORM,VK_FORMAT_R16G16B16A16_UNORM,
  VK_FORMAT_A2B10G10R10_UNORM_PACK32,VK_FORMAT_R16G16B16A16_SFLOAT,
  VK_FORMAT_R16G16_UINT,VK_FORMAT_R16G16B16A16_UINT,VK_FORMAT_R32_UINT,VK_FORMAT_R32G32_UINT};
 const VkPipelineRenderingCreateInfo *r=(const VkPipelineRenderingCreateInfo*)p->pNext;
 static const int ranks[8][8]={{-1,0,1,2,3,4,5,6},{7,-1,8,9,10,11,12,13},{14,15,-1,16,17,18,19,20},{21,22,23,-1,24,25,26,27},{28,29,30,31,-1,-1,-1,-1},{32,33,34,35,-1,-1,-1,-1},{36,37,38,39,-1,-1,-1,-1},{40,41,42,43,-1,-1,-1,-1}};
 unsigned i=(unsigned)(out-state.bank->pipeline),f=8,sv,dv;int failed;
 for(sv=0;sv<8;++sv)for(dv=0;dv<8;++dv)if(ranks[sv][dv]==(int)(i%44))f=dv;
 (void)a;
 if(d!=state.config->device || cache || n!=1 || i>=176 ||
    p->layout!=H(VkPipelineLayout,50) || p->stageCount!=2 ||
    p->pStages[0].module!=state.config->vertex ||
    p->pVertexInputState->vertexBindingDescriptionCount!=1 ||
    p->pVertexInputState->pVertexBindingDescriptions[0].stride!=16 ||
    p->pVertexInputState->pVertexBindingDescriptions[0].inputRate!=VK_VERTEX_INPUT_RATE_INSTANCE ||
    p->pMultisampleState->rasterizationSamples!=VK_SAMPLE_COUNT_1_BIT ||
    p->pMultisampleState->sampleShadingEnable ||
    p->pColorBlendState->attachmentCount!=1 ||
    p->pColorBlendState->pAttachments[0].colorWriteMask!=15 ||
    p->pColorBlendState->pAttachments[0].blendEnable)state.error=1;
 if(p->pStages[1].module!=H(VkShaderModule,100+state.modules-1))state.error=1;
 if(state.config->dynamic_rendering){
  if(!r || r->sType!=VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO ||
     r->colorAttachmentCount!=1 || *r->pColorAttachmentFormats!=formats[f] || p->renderPass)state.error=1;
 }else if(r || p->renderPass!=state.config->passes[f])state.error=1;
 failed=fail();
 /* Even a failed driver call may return a live partial pipeline. */
 if(state.calls!=state.null_at){state.pipeline_live[i]=1;++state.pipelines;*out=H(VkPipeline,200+i);}
 return failed?VK_ERROR_OUT_OF_DEVICE_MEMORY:VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL destroy_pipeline(VkDevice d,VkPipeline p,const VkAllocationCallbacks *a){
 unsigned i=(unsigned)((uintptr_t)p-200);(void)a;
 if(d!=state.config->device || i>=176 || !state.pipeline_live[i]){state.error=1;return;}state.pipeline_live[i]=0;
}
static int emit_packed(void *context,unsigned source,unsigned dest,unsigned dn,unsigned sn,xe_spv_binary *out){
 static const uint32_t words[5]={0x07230203u,0x10000,0,1,0};
 if(context!=state.config || source==dest || dn>1 || sn>1){state.error=1;return 0;}
 ++state.emit_calls;
 if(state.emit_calls==state.emit_fail)return 0;
 xe_spv_binary_reset(out);
 if(state.emit_calls==state.emit_empty && !state.emit_bad)return 1;
 if(!xe_spv_binary_reserve_exact(out,5))return 0;
 memcpy(out->words,words,sizeof(words));out->count=5;
 if(state.emit_calls==state.emit_empty && state.emit_bad==1)out->failed=1;
 if(state.emit_calls==state.emit_empty && state.emit_bad==2)out->count=out->capacity+1;
 return 1;
}
static int empty(void){
 unsigned i;if(state.error || state.layout)return 0;
 for(i=0;i<176;++i)if(state.shader_live[i])return 0;
 for(i=0;i<176;++i)if(state.pipeline_live[i])return 0;
 return 1;
}
#define CHECK(x) do{if(!(x)){fprintf(stderr,"color bank line%d calls%u error%u\n",__LINE__,state.calls,state.error);return 1;}}while(0)
int main(void){
 static const unsigned formats[8]={0,1,2,3,4,5,14,15};
 xe_transfer_color_vk_config config={0};xe_transfer_color_vk_bank bank={0};xe_spv_binary scratch={0};
 xe_transfer_color_vk_ops ops={create_shader,destroy_shader,create_layout,destroy_layout,create_pipelines,destroy_pipeline};
 xe_transfer_color_vk_ops missing;unsigned packed,dynamic,mode,index,s,d,n,before,total;
 static const int ranks[8][8]={{-1,0,1,2,3,4,5,6},{7,-1,8,9,10,11,12,13},{14,15,-1,16,17,18,19,20},{21,22,23,-1,24,25,26,27},{28,29,30,31,-1,-1,-1,-1},{32,33,34,35,-1,-1,-1,-1},{36,37,38,39,-1,-1,-1,-1},{40,41,42,43,-1,-1,-1,-1}};
 config.device=H(VkDevice,1);config.vertex=H(VkShaderModule,2);config.image_layout=H(VkDescriptorSetLayout,3);
 config.enabled_views=255;config.address.scale_x=3;config.address.scale_y=5;config.address.native2=0;
 for(d=0;d<8;++d)config.passes[d]=H(VkRenderPass,10+d);
 for(packed=0;packed<2;++packed){
 config.emit_packed=packed?emit_packed:NULL;config.emit_context=&config;
 for(dynamic=0;dynamic<2;++dynamic){
  config.dynamic_rendering=dynamic;
  for(mode=0;mode<3;++mode)for(index=mode?1:0;index<=(mode?353u:0u);++index){
   memset(&state,0,sizeof(state));state.bank=&bank;state.config=&config;
   state.fail_at=mode==1?index:0;state.null_at=mode==2?index:0;
   if(mode){CHECK(!xe_transfer_color_vk_init(&bank,&config,&ops,&scratch));CHECK(empty() && !bank.device && !bank.ready);}
   else{
    CHECK(xe_transfer_color_vk_init(&bank,&config,&ops,&scratch));CHECK(bank.ready && bank.packed_address==packed && state.modules==176 && state.pipelines==176 && state.calls==353);
    before=state.calls;
    CHECK(!xe_transfer_color_vk_init(&bank,&config,&ops,&scratch) && state.calls==before);
    for(total=0;total<1000;++total)for(n=0;n<4;++n)for(s=0;s<8;++s)for(d=0;d<8;++d){
     const VkPipeline *p=xe_transfer_color_vk_get(&bank,formats[s],formats[d],n>>1,n&1);
     if(ranks[s][d]<0){CHECK(!p);continue;}
     CHECK(p==bank.pipeline+n*44+ranks[s][d] && *p==H(VkPipeline,200+n*44+ranks[s][d]));
    }
    CHECK(!xe_transfer_color_vk_get(&bank,8,0,0,0) && !xe_transfer_color_vk_get(&bank,0,UINT32_MAX,0,0));
    CHECK(state.calls==before && !state.error);
    missing=ops;missing.destroy_pipeline=NULL;CHECK(!xe_transfer_color_vk_close(&bank,&missing) && bank.ready);
    CHECK(xe_transfer_color_vk_close(&bank,&ops) && empty());
   }
   CHECK(!xe_transfer_color_vk_get(&bank,0,1,0,0) && xe_transfer_color_vk_close(&bank,&ops));
  }
 }
 }
 /* A producer can fail, or claim success with no words, at every slot. */
 config.emit_packed=emit_packed;
 for(mode=0;mode<4;++mode)for(index=1;index<=176;++index){
  memset(&state,0,sizeof(state));state.bank=&bank;state.config=&config;
  state.emit_fail=mode?0:index;state.emit_empty=mode?index:0;state.emit_bad=mode>1?mode-1:0;
  CHECK(!xe_transfer_color_vk_init(&bank,&config,&ops,&scratch));
  CHECK(empty() && !bank.ready && !bank.device && state.emit_calls==index);
 }
 config.emit_packed=NULL;
 /* Every nonempty supported-view subset, without relying on dense modules. */
 for(index=1;index<256;++index){
  memset(&state,0,sizeof(state));state.bank=&bank;state.config=&config;config.enabled_views=index;
  CHECK(xe_transfer_color_vk_init(&bank,&config,&ops,&scratch));before=state.calls;
  for(s=0;s<8;++s)for(d=0;d<8;++d)
   CHECK((xe_transfer_color_vk_get(&bank,formats[s],formats[d],0,0)!=NULL)==(ranks[s][d]>=0 && (index&(1u<<s))!=0 && (index&(1u<<d))!=0));
  CHECK(state.calls==before && xe_transfer_color_vk_close(&bank,&ops) && empty());
 }
 memset(&state,0,sizeof(state));state.bank=&bank;state.config=&config;
 config.enabled_views=255;config.address.scale_x=config.address.scale_y=1;
 CHECK(xe_transfer_color_vk_init(&bank,&config,&ops,&scratch));
 CHECK(bank.scale_classes==1 && state.modules==44 && state.pipelines==44 && state.calls==89);
 before=state.calls;
 for(n=0;n<4;++n)CHECK(xe_transfer_color_vk_get(&bank,0,1,n>>1,n&1)==bank.pipeline);
 CHECK(xe_transfer_color_vk_get(&bank,10,14,0,0)==xe_transfer_color_vk_get(&bank,2,14,0,0));
 CHECK(xe_transfer_color_vk_get(&bank,6,0,0,0)==xe_transfer_color_vk_get(&bank,4,0,0,0));
 CHECK(!xe_transfer_color_vk_get(&bank,0,1,2,0) && !xe_transfer_color_vk_get(&bank,0,1,0,2));
 CHECK(state.calls==before && xe_transfer_color_vk_close(&bank,&ops) && empty());
 xe_spv_binary_close(&scratch);
 puts("PASS: color bank exact states, 706 driver failure/null sites per rendering mode and producer, 704 invalid producer outputs, 255 view masks, 1024000 immutable lookups, partial ownership and repeated close");return 0;
}
