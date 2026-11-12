/* Prepared-bank failure and ownership tests; MSVC C89, no GPU required. */
#include "libretro/core_transfer_uint_d3d12.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"line %u: %s\n",(unsigned)__LINE__,#x);exit(1);}} while(0)
static unsigned root_calls,pipeline_calls,emit_calls,released,roots_released,live_shader;
static unsigned failure,fail_at,scaled,native2;
static unsigned char bytecode[4],vertex[4],root_token,pipeline_tokens[144];
static xe_transfer_address_key captured_key;
static xe_transfer_uint_options captured_options;
static HRESULT create_root(ID3D12Device *device,const D3D12_ROOT_SIGNATURE_DESC *desc,ID3D12RootSignature **out) {
 CHECK(device==(ID3D12Device*)vertex);CHECK(!root_calls++);
 CHECK(desc->NumParameters==2);CHECK(desc->pParameters[0].DescriptorTable.pDescriptorRanges[0].NumDescriptors==1);
 CHECK(desc->pParameters[1].Constants.Num32BitValues==28);
 CHECK(desc->pParameters[1].Constants.ShaderRegister==1 && desc->pParameters[1].Constants.RegisterSpace==31);
 if(failure==1){*out=(ID3D12RootSignature*)1;return E_FAIL;}
 if(failure==2)return S_OK;
 *out=(ID3D12RootSignature*)&root_token;return S_OK;
}
static int emit(const xe_transfer_uint_options *options,const xe_transfer_address_key *key,unsigned format,XeDxilShaderBinary *out) {
 unsigned index=emit_calls++,scale=index/36,f=index/9%4,dm=index/3%3,sm=index%3;
 static const unsigned formats[4]={4,5,14,15};
 CHECK(!live_shader);CHECK(format==formats[f]);CHECK(options->target==0);
 CHECK(options->sample_mode==(dm?XE_TRANSFER_UINT_SAMPLE_BUILTIN:(unsigned)XE_TRANSFER_UINT_SINGLE_SAMPLE));
 CHECK(options->source_multisampled==(unsigned)(sm!=0));CHECK(key->source_msaa==sm && key->dest_msaa==dm);
 CHECK(key->source_native2==native2 && key->dest_native2==native2);
 CHECK(key->source_scale_x==((scale&1)?1u:scaled?3u:1u));
 CHECK(key->source_scale_y==((scale&1)?1u:scaled?5u:1u));
 CHECK(key->dest_scale_x==((scale&2)?1u:scaled?3u:1u));
 CHECK(key->dest_scale_y==((scale&2)?1u:scaled?5u:1u));
 captured_key=*key;captured_options=*options;
 out->data=bytecode;out->size=sizeof(bytecode);live_shader=1;
 return !(failure==3 && index==fail_at);
}
static void free_shader(XeDxilShaderBinary *shader) {
 if(shader->data){CHECK(live_shader);CHECK(shader->data==bytecode);live_shader=0;}
 memset(shader,0,sizeof(*shader));
}
static HRESULT create_pipeline(ID3D12Device *device,const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc,ID3D12PipelineState **out) {
 unsigned index=pipeline_calls++,dm=index/3%3,f=index/9%4;
 static const DXGI_FORMAT views[4]={DXGI_FORMAT_R16G16_UINT,DXGI_FORMAT_R16G16B16A16_UINT,DXGI_FORMAT_R32_UINT,DXGI_FORMAT_R32G32_UINT};
 CHECK(device==(ID3D12Device*)vertex);CHECK(desc->pRootSignature==(ID3D12RootSignature*)&root_token);
 CHECK(live_shader && desc->PS.pShaderBytecode==bytecode && desc->PS.BytecodeLength==4);
 CHECK(desc->VS.pShaderBytecode==vertex && desc->VS.BytecodeLength==4);
 CHECK(desc->NumRenderTargets==1 && desc->RTVFormats[0]==views[f]);
 CHECK(desc->SampleDesc.Count==(dm==1 && !native2?4:1u<<dm));
 CHECK(desc->SampleMask==(dm==1 && !native2?9:UINT32_MAX));
 CHECK(!desc->DepthStencilState.DepthEnable && !desc->DepthStencilState.StencilEnable);
 CHECK(desc->InputLayout.NumElements==1 && desc->InputLayout.pInputElementDescs[0].InstanceDataStepRate==1);
 CHECK(captured_key.dest_msaa==dm && captured_options.target==0);
 if(index==fail_at && failure==4){*out=(ID3D12PipelineState*)1;return E_FAIL;}
 if(index==fail_at && failure==5)return S_OK;
 *out=(ID3D12PipelineState*)&pipeline_tokens[index];return S_OK;
}
static void release_pipeline(ID3D12PipelineState *pipeline) {
 CHECK(!roots_released);CHECK(released<pipeline_calls);
 CHECK(pipeline==(ID3D12PipelineState*)&pipeline_tokens[released]);++released;
}
static void release_root(ID3D12RootSignature *root) {
 CHECK(root==(ID3D12RootSignature*)&root_token);CHECK(!roots_released++);
}
static const xe_transfer_uint_d3d12_ops ops={create_root,create_pipeline,release_root,release_pipeline,emit,free_shader};
static void reset(void){root_calls=pipeline_calls=emit_calls=released=roots_released=live_shader=0;}
static void run(unsigned mode,unsigned position) {
 xe_transfer_uint_d3d12_bank bank={0},zero={0},snapshot;
 xe_transfer_uint_d3d12_config config={0};xe_transfer_uint_d3d12_ops incomplete=ops;
 unsigned count=scaled?144:36,scale,f,dm,sm,created;int result;
 static const unsigned formats[4]={4,5,14,15};
 reset();failure=mode;fail_at=position;
 config.device=(ID3D12Device*)vertex;config.vertex=vertex;config.vertex_bytes=4;
 config.native2=native2;config.scale_x=scaled?3u:1u;config.scale_y=scaled?5u:1u;
 CHECK(!xe_transfer_uint_d3d12_get(&bank,4,0,0,0,0));
 result=xe_transfer_uint_d3d12_init(&bank,&config,&ops);
 CHECK(!live_shader);
 if(mode){
  CHECK(!result && !memcmp(&bank,&zero,sizeof(bank)));
  created=mode<=2?0:position;CHECK(released==created);CHECK(roots_released==(mode<=2?0u:1u));
 } else {
  CHECK(result && pipeline_calls==count && emit_calls==count);
  for(scale=0;scale<4;++scale)for(f=0;f<4;++f)for(dm=0;dm<3;++dm)for(sm=0;sm<3;++sm){
   unsigned index=((((scaled?scale:0)*4+f)*3+dm)*3+sm);
   CHECK(xe_transfer_uint_d3d12_get(&bank,formats[f],dm,sm,scale>>1,scale&1)==(ID3D12PipelineState*)&pipeline_tokens[index]);
   if(f<2)CHECK(xe_transfer_uint_d3d12_get(&bank,formats[f]+2,dm,sm,scale>>1,scale&1)==(ID3D12PipelineState*)&pipeline_tokens[index]);
  }
  CHECK(!xe_transfer_uint_d3d12_get(&bank,0,0,0,0,0));CHECK(!xe_transfer_uint_d3d12_get(&bank,4,3,0,0,0));
  CHECK(!xe_transfer_uint_d3d12_get(&bank,4,0,0,2,0));CHECK(pipeline_calls==count && emit_calls==count);
  snapshot=bank;incomplete.release_pipeline=NULL;
  CHECK(!xe_transfer_uint_d3d12_close(&bank,&incomplete) && !memcmp(&snapshot,&bank,sizeof(bank)));
  CHECK(!xe_transfer_uint_d3d12_init(&bank,&config,&ops));
  CHECK(xe_transfer_uint_d3d12_close(&bank,&ops));CHECK(released==count && roots_released==1);
 }
 CHECK(xe_transfer_uint_d3d12_close(&bank,&ops));CHECK(!memcmp(&bank,&zero,sizeof(bank)));
}
int main(void) {
 unsigned mode,index,runs=0;
 for(scaled=0;scaled<2;++scaled)for(native2=0;native2<2;++native2){
  run(0,0);++runs;run(1,0);++runs;run(2,0);++runs;
  for(mode=3;mode<=5;++mode)for(index=0;index<(scaled?144u:36u);++index){run(mode,index);++runs;}
 }
 printf("PASS: %u prepared-bank ownership/failure cases; 36/144 PSOs, aliases, immutable lookup, release order\n",runs);return 0;
}


