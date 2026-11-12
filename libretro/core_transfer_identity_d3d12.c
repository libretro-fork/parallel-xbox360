/* Fixed final handle storage; shared float modules, typed resources. BSD; C89. */
#include "core_transfer_identity_d3d12.h"
#include "core_transfer_uint_d3d12.h"
#include "core_draw_stats.h"
#include <string.h>
int xe_transfer_identity_d3d12_native_ops(xe_transfer_identity_d3d12_ops *out)
{
 const xe_transfer_uint_d3d12_ops *driver;
 if(!out)return 0;
 driver=xe_transfer_uint_d3d12_native_ops();
 out->create_root=driver->create_root;out->create_pipeline=driver->create_pipeline;
 out->release_root=driver->release_root;out->release_pipeline=driver->release_pipeline;
 out->emit=xe_dxil_transfer_identity_shader;out->free_shader=xe_dxil_shader_binary_free;return 1;
}
ID3D12PipelineState *xe_transfer_identity_d3d12_get(const xe_transfer_identity_d3d12_bank *bank,
 unsigned view,unsigned dest,unsigned source,unsigned dest_native,unsigned source_native)
{
 unsigned scale;
 if(!bank || !bank->ready || view>=XE_TRANSFER_IDENTITY_VIEWS || dest>2 || source>2 ||
  dest_native>1 || source_native>1 || !(bank->sample_mask[view]&(1u<<dest)) ||
  !(bank->sample_mask[view]&(1u<<source)))return NULL;
 scale=bank->scale_classes==1?0:dest_native*2+source_native;
 return bank->pipeline[((scale*4+view)*3+dest)*3+source];
}
int xe_transfer_identity_d3d12_close(xe_transfer_identity_d3d12_bank *bank,
 const xe_transfer_identity_d3d12_ops *ops)
{
 unsigned i;
 if(!bank)return 0;
 if(bank->device) {
  if(!ops || !ops->release_pipeline || !ops->release_root)return 0;
  for(i=0;i<XE_TRANSFER_IDENTITY_D3D12_PIPELINES;++i)
   if(bank->pipeline[i])ops->release_pipeline(bank->pipeline[i]);
  if(bank->root)ops->release_root(bank->root);
 }
 memset(bank,0,sizeof(*bank));return 1;
}
int xe_transfer_identity_d3d12_init(xe_transfer_identity_d3d12_bank *bank,
 const xe_transfer_identity_d3d12_config *config,const xe_transfer_identity_d3d12_ops *ops)
{
 static const DXGI_FORMAT views[4]={DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R16G16B16A16_UNORM,
  DXGI_FORMAT_R10G10B10A2_UNORM,DXGI_FORMAT_R16G16B16A16_FLOAT};
 D3D12_ROOT_SIGNATURE_DESC root={0};D3D12_ROOT_PARAMETER parameters[2]={{0}};D3D12_DESCRIPTOR_RANGE range={0};
 D3D12_GRAPHICS_PIPELINE_STATE_DESC desc={0};D3D12_INPUT_ELEMENT_DESC input={0};
 ID3D12RootSignature *created_root=NULL;ID3D12PipelineState *created_pipeline;
 xe_transfer_address_key key={0};xe_transfer_identity_options options={0};XeDxilShaderBinary pixel={0};
 unsigned scale,f,dm,sm,slot,needed;int success=0;
 if(!bank || bank->device || !config || !ops || !config->device || !config->vertex || !config->vertex_bytes ||
  config->native2>1 || !config->scale_x || !config->scale_y ||
  config->scale_x>UINT32_MAX/80 || config->scale_y>UINT32_MAX/16 ||
  !ops->create_root || !ops->create_pipeline || !ops->release_root || !ops->release_pipeline ||
  !ops->emit || !ops->free_shader)return 0;
 for(f=0;f<4;++f)if(config->sample_mask[f]>7)return 0;
 bank->device=config->device;bank->native2=config->native2;
 bank->scale_classes=config->scale_x==1 && config->scale_y==1?1:4;
 for(f=0;f<4;++f)bank->sample_mask[f]=config->sample_mask[f];
 range.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;range.NumDescriptors=1;
 parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
 parameters[0].DescriptorTable.NumDescriptorRanges=1;parameters[0].DescriptorTable.pDescriptorRanges=&range;
 parameters[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
 parameters[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
 parameters[1].Constants.ShaderRegister=1;parameters[1].Constants.RegisterSpace=31;
 parameters[1].Constants.Num32BitValues=5;parameters[1].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
 root.NumParameters=2;root.pParameters=parameters;root.Flags=D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
 if(FAILED(ops->create_root(bank->device,&root,&created_root)) || !created_root)goto done;
 bank->root=created_root;
 input.SemanticName="POSITION";input.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;
 input.InputSlotClass=D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA;input.InstanceDataStepRate=1;
 desc.pRootSignature=bank->root;desc.VS.pShaderBytecode=config->vertex;desc.VS.BytecodeLength=config->vertex_bytes;
 desc.InputLayout.NumElements=1;desc.InputLayout.pInputElementDescs=&input;
 desc.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
 desc.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;desc.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
 desc.RasterizerState.DepthClipEnable=TRUE;desc.BlendState.RenderTarget[0].RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
 desc.NumRenderTargets=1;
 key.source_color=key.dest_color=1;key.source_pitch=key.dest_pitch=1;
 key.source_native2=key.dest_native2=bank->native2;
 for(scale=0;scale<bank->scale_classes;++scale)for(dm=0;dm<3;++dm)for(sm=0;sm<3;++sm) {
  needed=0;for(f=0;f<4;++f)if((bank->sample_mask[f]&(1u<<dm)) &&
   (bank->sample_mask[f]&(1u<<sm)))needed=1;
  if(!needed)continue;
  key.source_scale_x=scale&1?1:config->scale_x;key.source_scale_y=scale&1?1:config->scale_y;
  key.dest_scale_x=scale&2?1:config->scale_x;key.dest_scale_y=scale&2?1:config->scale_y;
  key.dest_msaa=dm;key.source_msaa=sm;
  options.source_multisampled=sm!=0;options.sample_mode=dm?XE_TRANSFER_IDENTITY_SAMPLE_BUILTIN:XE_TRANSFER_IDENTITY_SINGLE_SAMPLE;
  if(!ops->emit(&options,&key,&pixel) || !pixel.data || !pixel.size)goto done;
  desc.PS.pShaderBytecode=pixel.data;desc.PS.BytecodeLength=pixel.size;
  desc.SampleDesc.Count=dm==1 && !bank->native2?4:1u<<dm;desc.SampleMask=dm==1 && !bank->native2?9:UINT32_MAX;
  for(f=0;f<4;++f) {
   if(!(bank->sample_mask[f]&(1u<<dm)) || !(bank->sample_mask[f]&(1u<<sm)))continue;
   desc.RTVFormats[0]=views[f];created_pipeline=NULL;
   XE_DRAW_EVENT(XE_DRAW_UTILITY_PIPELINE,XE_DRAW_GRAPHICS_PSO);
   if(FAILED(ops->create_pipeline(bank->device,&desc,&created_pipeline)) || !created_pipeline)goto done;
   slot=(((scale*4+f)*3+dm)*3+sm);bank->pipeline[slot]=created_pipeline;
  }
  ops->free_shader(&pixel);
 }
 bank->ready=1;success=1;
done:
 ops->free_shader(&pixel);
 if(!success)xe_transfer_identity_d3d12_close(bank,ops);
 return success;
}
