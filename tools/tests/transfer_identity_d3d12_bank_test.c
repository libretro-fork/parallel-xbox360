/* Driver recording, full partial-failure unwind and immutable lookup. BSD; C89. */
#include "libretro/core_transfer_identity_d3d12.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"identity bank line%d\n",__LINE__);exit(1);}}while(0)
static unsigned char vertex[4],pixel[4],root_token,tokens[144],live[144];
static unsigned root_calls,pipeline_calls,emit_calls,roots,released,created,shader_live;
static unsigned mode,fail_at,scaled,native2,last_released,mask[4];
static xe_transfer_address_key captured;
static HRESULT root_create(ID3D12Device *device,const D3D12_ROOT_SIGNATURE_DESC *desc,ID3D12RootSignature **out)
{
 CHECK(device==(ID3D12Device*)vertex);CHECK(!root_calls++);
 CHECK(desc->NumParameters==2 && desc->pParameters[0].DescriptorTable.pDescriptorRanges[0].NumDescriptors==1);
 CHECK(desc->pParameters[1].Constants.Num32BitValues==5 &&
  desc->pParameters[1].Constants.ShaderRegister==1 && desc->pParameters[1].Constants.RegisterSpace==31);
 if(mode==1){*out=(ID3D12RootSignature*)1;return E_FAIL;}
 if(mode==2)return S_OK;
 *out=(ID3D12RootSignature*)&root_token;return S_OK;
}
static int shader_emit(const xe_transfer_identity_options *options,const xe_transfer_address_key *key,XeDxilShaderBinary *out)
{
 unsigned index=emit_calls++;
 CHECK(!shader_live && options->view==0 && options->target==0);
 CHECK(options->source_multisampled==(unsigned)(key->source_msaa!=0));
 CHECK(options->sample_mode==(key->dest_msaa?XE_TRANSFER_IDENTITY_SAMPLE_BUILTIN:(unsigned)XE_TRANSFER_IDENTITY_SINGLE_SAMPLE));
 CHECK(!key->dest_wide && !key->source_wide && key->dest_color && key->source_color);
 CHECK(key->source_native2==native2 && key->dest_native2==native2);
 CHECK(key->source_scale_x==1 || key->source_scale_x==3);
 CHECK(key->source_scale_y==(key->source_scale_x==1?1u:5u));
 CHECK(key->dest_scale_y==(key->dest_scale_x==1?1u:5u));
 captured=*key;out->data=pixel;out->size=4;shader_live=1;
 return !(mode==3 && index==fail_at);
}
static void shader_free(XeDxilShaderBinary *shader)
{
 if(shader->data){CHECK(shader_live && shader->data==pixel);shader_live=0;}
 memset(shader,0,sizeof(*shader));
}
static HRESULT pipeline_create(ID3D12Device *device,const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc,ID3D12PipelineState **out)
{
 static const DXGI_FORMAT views[4]={DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R16G16B16A16_UNORM,
  DXGI_FORMAT_R10G10B10A2_UNORM,DXGI_FORMAT_R16G16B16A16_FLOAT};
 unsigned f,scale,slot,index=pipeline_calls++;
 CHECK(device==(ID3D12Device*)vertex && desc->pRootSignature==(ID3D12RootSignature*)&root_token);
 CHECK(shader_live && desc->PS.pShaderBytecode==pixel && desc->PS.BytecodeLength==4);
 CHECK(desc->VS.pShaderBytecode==vertex && desc->VS.BytecodeLength==4 && desc->NumRenderTargets==1);
 for(f=0;f<4 && desc->RTVFormats[0]!=views[f];++f){}
 CHECK(f<4 && (mask[f]&(1u<<captured.dest_msaa)) && (mask[f]&(1u<<captured.source_msaa)));
 CHECK(desc->SampleDesc.Count==(captured.dest_msaa==1 && !native2?4:1u<<captured.dest_msaa));
 CHECK(desc->SampleMask==(captured.dest_msaa==1 && !native2?9:UINT32_MAX));
 CHECK(!desc->DepthStencilState.DepthEnable && !desc->DepthStencilState.StencilEnable);
 CHECK(!desc->BlendState.RenderTarget[0].BlendEnable && desc->BlendState.RenderTarget[0].RenderTargetWriteMask==15);
 CHECK(desc->InputLayout.NumElements==1 && desc->InputLayout.pInputElementDescs[0].InstanceDataStepRate==1);
 if(mode==4 && index==fail_at){*out=(ID3D12PipelineState*)1;return E_FAIL;}
 if(mode==5 && index==fail_at)return S_OK;
 scale=scaled?((captured.dest_scale_x==1?2u:0u)|(captured.source_scale_x==1?1u:0u)):0;
 slot=((scale*4+f)*3+captured.dest_msaa)*3+captured.source_msaa;
 CHECK(slot<144 && !live[slot]);live[slot]=1;++created;
 *out=(ID3D12PipelineState*)&tokens[slot];return S_OK;
}
static void pipeline_release(ID3D12PipelineState *pipeline)
{
 ptrdiff_t slot=(unsigned char*)pipeline-tokens;
 CHECK(!roots && slot>=0 && slot<144 && live[slot]);
 if(released)CHECK((unsigned)slot>last_released);
 last_released=(unsigned)slot;live[slot]=0;++released;
}
static void root_release(ID3D12RootSignature *root)
{
 CHECK(root==(ID3D12RootSignature*)&root_token && !roots++ && !shader_live && released==created);
}
static const xe_transfer_identity_d3d12_ops ops={root_create,pipeline_create,root_release,pipeline_release,shader_emit,shader_free};
static void run(unsigned failure,unsigned position,unsigned sparse)
{
 xe_transfer_identity_d3d12_bank bank={0},zero={0},snapshot;
 xe_transfer_identity_d3d12_config config={0};xe_transfer_identity_d3d12_ops incomplete=ops;
 unsigned f,dm,sm,scale,slot,expected=0;int result;
 root_calls=pipeline_calls=emit_calls=roots=released=created=shader_live=0;
 memset(live,0,sizeof(live));mode=failure;fail_at=position;
 for(f=0;f<4;++f)mask[f]=sparse==2?0:sparse?(f==0?7u:f==1?0u:f==2?5u:1u):7u;
 config.device=(ID3D12Device*)vertex;config.vertex=vertex;config.vertex_bytes=4;
 config.scale_x=scaled?3u:1u;config.scale_y=scaled?5u:1u;config.native2=native2;
 for(f=0;f<4;++f)config.sample_mask[f]=mask[f];
 CHECK(!xe_transfer_identity_d3d12_get(&bank,0,0,0,0,0));
 result=xe_transfer_identity_d3d12_init(&bank,&config,&ops);CHECK(!shader_live);
 if(mode)CHECK(!result && !memcmp(&bank,&zero,sizeof(bank)));
 else {
  CHECK(result && bank.ready);
  for(scale=0;scale<4;++scale)for(f=0;f<4;++f)for(dm=0;dm<3;++dm)for(sm=0;sm<3;++sm) {
   ID3D12PipelineState *found=xe_transfer_identity_d3d12_get(&bank,f,dm,sm,scale>>1,scale&1);
   slot=(((scaled?scale:0)*4+f)*3+dm)*3+sm;
   if((mask[f]&(1u<<dm)) && (mask[f]&(1u<<sm)))CHECK(found==(ID3D12PipelineState*)&tokens[slot]);
   else CHECK(!found);
  }
  for(f=0;f<4;++f)for(dm=0;dm<3;++dm)for(sm=0;sm<3;++sm)
   if((mask[f]&(1u<<dm)) && (mask[f]&(1u<<sm)))++expected;
  expected*=scaled?4:1;CHECK(pipeline_calls==expected && created==expected);
  if(!sparse)CHECK(emit_calls==(scaled?36u:9u));
  CHECK(!xe_transfer_identity_d3d12_get(&bank,4,0,0,0,0));
  CHECK(!xe_transfer_identity_d3d12_get(&bank,0,3,0,0,0));
  CHECK(!xe_transfer_identity_d3d12_get(&bank,0,0,0,2,0));
  snapshot=bank;incomplete.release_pipeline=NULL;
  CHECK(!xe_transfer_identity_d3d12_close(&bank,&incomplete) && !memcmp(&bank,&snapshot,sizeof(bank)));
  CHECK(!xe_transfer_identity_d3d12_init(&bank,&config,&ops));
  CHECK(xe_transfer_identity_d3d12_close(&bank,&ops));
 }
 CHECK(released==created && roots==(mode==1 || mode==2?0u:1u));
 for(f=0;f<144;++f)CHECK(!live[f]);
 CHECK(xe_transfer_identity_d3d12_close(&bank,&ops) && !memcmp(&bank,&zero,sizeof(bank)));
}
int main(void)
{
 unsigned index,runs=0;
 for(scaled=0;scaled<2;++scaled)for(native2=0;native2<2;++native2) {
  run(0,0,0);run(0,0,1);run(0,0,2);run(1,0,0);run(2,0,0);runs+=5;
  for(index=0;index<(scaled?36u:9u);++index){run(3,index,0);++runs;}
  for(index=0;index<(scaled?144u:36u);++index){run(4,index,0);run(5,index,0);runs+=2;}
 }
 printf("PASS: %u D3D12 identity bank failures/ownership cases, shared modules, sparse caps and lookup without creation\n",runs);return 0;
}
