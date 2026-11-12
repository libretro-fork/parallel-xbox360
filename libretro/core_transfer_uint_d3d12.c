/* Dense startup table; native DXIL and final handle ownership. BSD; C89. */
#define COBJMACROS

#include "core_transfer_uint_d3d12.h"
#include "core_draw_stats.h"
#include <string.h>
static HRESULT native_root(ID3D12Device *device,const D3D12_ROOT_SIGNATURE_DESC *desc,ID3D12RootSignature **out) {
 ID3DBlob *serialized=NULL,*error=NULL;ID3D12RootSignature *root=NULL;
 HRESULT status=D3D12SerializeRootSignature(desc,D3D_ROOT_SIGNATURE_VERSION_1,&serialized,&error);
 if(SUCCEEDED(status)) {
  if(!serialized)status=E_FAIL;
  else status=ID3D12Device_CreateRootSignature(device,0,ID3D10Blob_GetBufferPointer(serialized),
   ID3D10Blob_GetBufferSize(serialized),&IID_ID3D12RootSignature,(void**)&root);
 }
 if(error)ID3D10Blob_Release(error);
 if(serialized)ID3D10Blob_Release(serialized);
 if(SUCCEEDED(status) && root)*out=root;
 else if(SUCCEEDED(status))status=E_FAIL;
 return status;
}
static HRESULT native_pipeline(ID3D12Device *device,const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc,ID3D12PipelineState **out) {
 ID3D12PipelineState *pipeline=NULL;
 HRESULT status=ID3D12Device_CreateGraphicsPipelineState(device,desc,&IID_ID3D12PipelineState,(void**)&pipeline);
 if(SUCCEEDED(status) && pipeline)*out=pipeline;
 else if(SUCCEEDED(status))status=E_FAIL;
 return status;
}
static void native_release_root(ID3D12RootSignature *root){ID3D12RootSignature_Release(root);}
static void native_release_pipeline(ID3D12PipelineState *pipeline){ID3D12PipelineState_Release(pipeline);}
const xe_transfer_uint_d3d12_ops *xe_transfer_uint_d3d12_native_ops(void) {
 static const xe_transfer_uint_d3d12_ops ops={native_root,native_pipeline,native_release_root,
  native_release_pipeline,xe_dxil_transfer_uint_shader_shape,xe_dxil_shader_binary_free};
 return &ops;
}
static int format_class(unsigned format,unsigned *out) {
 switch(format) {
  case 4:case 6:*out=0;return 1;
  case 5:case 7:*out=1;return 1;
  case 14:*out=2;return 1;
  case 15:*out=3;return 1;
  default:return 0;
 }
}
ID3D12PipelineState *xe_transfer_uint_d3d12_get(const xe_transfer_uint_d3d12_bank *bank,
 unsigned format,unsigned dest_msaa,unsigned source_msaa,unsigned dest_native,unsigned source_native) {
 unsigned f,group;
 if(!bank || !bank->ready || dest_msaa>2 || source_msaa>2 || dest_native>1 || source_native>1 ||
    !format_class(format,&f))return NULL;
 group=(bank->scale_classes==1?0:dest_native*2+source_native)*4+f;
 return bank->pipeline[(group*3+dest_msaa)*3+source_msaa];
}
int xe_transfer_uint_d3d12_close(xe_transfer_uint_d3d12_bank *bank,const xe_transfer_uint_d3d12_ops *ops) {
 unsigned i;
 if(!bank)return 0;
 if(bank->device) {
  if(!ops || !ops->release_pipeline || !ops->release_root)return 0;
  for(i=0;i<XE_TRANSFER_UINT_D3D12_PIPELINES;++i)
   if(bank->pipeline[i])ops->release_pipeline(bank->pipeline[i]);
  if(bank->root)ops->release_root(bank->root);
 }
 memset(bank,0,sizeof(*bank));return 1;
}
int xe_transfer_uint_d3d12_init(xe_transfer_uint_d3d12_bank *bank,
 const xe_transfer_uint_d3d12_config *config,const xe_transfer_uint_d3d12_ops *ops) {
 static const unsigned formats[4]={4,5,14,15};
 static const DXGI_FORMAT views[4]={DXGI_FORMAT_R16G16_UINT,DXGI_FORMAT_R16G16B16A16_UINT,DXGI_FORMAT_R32_UINT,DXGI_FORMAT_R32G32_UINT};
 D3D12_ROOT_SIGNATURE_DESC root={0};D3D12_ROOT_PARAMETER parameters[2]={{0}};D3D12_DESCRIPTOR_RANGE range={0};
 D3D12_GRAPHICS_PIPELINE_STATE_DESC desc={0};D3D12_INPUT_ELEMENT_DESC input={0};
 ID3D12RootSignature *created_root=NULL;ID3D12PipelineState *created_pipeline;
 xe_transfer_address_key key={0};xe_transfer_uint_options options={0};XeDxilShaderBinary pixel={0};
 unsigned scale,f,dm,sm,slot;int success=0;
 if(!bank || bank->device || !config || !ops || !config->device || !config->vertex || !config->vertex_bytes ||
    config->native2>1 || !config->scale_x || !config->scale_y ||
    config->scale_x>UINT32_MAX/80 || config->scale_y>UINT32_MAX/16 ||
    !ops->create_root || !ops->create_pipeline || !ops->release_root || !ops->release_pipeline ||
    !ops->emit || !ops->free_shader)return 0;
 bank->device=config->device;bank->native2=config->native2;
 bank->scale_classes=config->scale_x==1 && config->scale_y==1?1:4;
 range.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;range.NumDescriptors=1;
 parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
 parameters[0].DescriptorTable.NumDescriptorRanges=1;parameters[0].DescriptorTable.pDescriptorRanges=&range;
 parameters[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
 parameters[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
 parameters[1].Constants.ShaderRegister=1;parameters[1].Constants.RegisterSpace=31;
 parameters[1].Constants.Num32BitValues=(UINT)(sizeof(xe_transfer_uint_packet)/sizeof(uint32_t));
 parameters[1].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
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
 for(scale=0;scale<bank->scale_classes;++scale)for(f=0;f<4;++f)
 for(dm=0;dm<3;++dm)for(sm=0;sm<3;++sm) {
  key.source_scale_x=scale&1?1:config->scale_x;key.source_scale_y=scale&1?1:config->scale_y;
  key.dest_scale_x=scale&2?1:config->scale_x;key.dest_scale_y=scale&2?1:config->scale_y;
  key.dest_wide=f&1;key.dest_msaa=dm;key.source_msaa=sm;
  options.source_multisampled=sm!=0;options.sample_mode=dm?XE_TRANSFER_UINT_SAMPLE_BUILTIN:XE_TRANSFER_UINT_SINGLE_SAMPLE;
  XE_DRAW_EVENT(XE_DRAW_UTILITY_PIPELINE,XE_DRAW_TRANSLATE);
  if(!ops->emit(&options,&key,formats[f],&pixel) || !pixel.data || !pixel.size)goto done;
  desc.PS.pShaderBytecode=pixel.data;desc.PS.BytecodeLength=pixel.size;
  desc.RTVFormats[0]=views[f];desc.SampleDesc.Count=dm==1 && !bank->native2?4:1u<<dm;
  desc.SampleMask=dm==1 && !bank->native2?9:UINT32_MAX;
  created_pipeline=NULL;
  XE_DRAW_EVENT(XE_DRAW_UTILITY_PIPELINE,XE_DRAW_GRAPHICS_PSO);
  if(FAILED(ops->create_pipeline(bank->device,&desc,&created_pipeline)) || !created_pipeline)goto done;
  slot=(((scale*4+f)*3+dm)*3+sm);
  bank->pipeline[slot]=created_pipeline;
  ops->free_shader(&pixel);
 }
 bank->ready=1;success=1;
done:
 ops->free_shader(&pixel);
 if(!success)xe_transfer_uint_d3d12_close(bank,ops);
 return success;
}
