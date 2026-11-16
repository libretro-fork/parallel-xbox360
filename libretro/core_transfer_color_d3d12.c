/* Fixed initialization-only color PSO owners. BSD license. MSVC C89. */
#include "core_transfer_color_d3d12.h"
#include "core_transfer_uint_d3d12.h"
#include "core_draw_stats.h"
#include <string.h>
DXGI_FORMAT xe_transfer_color_d3d12_view(unsigned view)
{
 static const DXGI_FORMAT formats[8]={DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R16G16B16A16_UNORM,
  DXGI_FORMAT_R10G10B10A2_UNORM,DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R16G16_UINT,
  DXGI_FORMAT_R16G16B16A16_UINT,DXGI_FORMAT_R32_UINT,DXGI_FORMAT_R32G32_UINT};
 return view<8?formats[view]:DXGI_FORMAT_UNKNOWN;
}
int xe_transfer_color_d3d12_native_ops(xe_transfer_color_d3d12_ops *out)
{
 const xe_transfer_uint_d3d12_ops *driver;
 if(!out)return 0;
 driver=xe_transfer_uint_d3d12_native_ops();
 out->create_pipeline=driver->create_pipeline;out->release_pipeline=driver->release_pipeline;
 out->emit=xe_dxil_build_edram_transfer;out->free_shader=xe_dxil_shader_binary_free;return 1;
}
static int color_index(unsigned s,unsigned d,unsigned converted,unsigned *index)
{
 if(converted){if(s!=3 || d!=0)return 0;*index=44;return 1;}
 return xe_transfer_color_pair(s,d,index);
}
ID3D12PipelineState *const *xe_transfer_color_d3d12_get(const xe_transfer_color_d3d12_bank *bank,
 unsigned source_format,unsigned dest_format,unsigned dest_native,unsigned source_native,unsigned value_convert)
{
 unsigned s,d,scale,index;
 if(!bank || !bank->ready || dest_native>1 || source_native>1 || value_convert>1 ||
  !xe_transfer_color_class(source_format,&s) || !xe_transfer_color_class(dest_format,&d) ||
  !(bank->enabled_views&(1u<<s)) || !(bank->enabled_views&(1u<<d)) ||
  !color_index(s,d,value_convert,&index))return NULL;
 scale=bank->scale_classes==1?0:dest_native*2+source_native;
 index+=scale*XE_TRANSFER_COLOR_D3D12_PAIRS;
 return bank->pipeline[index]?bank->pipeline+index:NULL;
}
int xe_transfer_color_d3d12_close(xe_transfer_color_d3d12_bank *bank,
 const xe_transfer_color_d3d12_ops *ops)
{
 unsigned i;
 if(!bank)return 0;
 if(bank->device){
  if(!ops || !ops->release_pipeline)return 0;
  for(i=0;i<XE_TRANSFER_COLOR_D3D12_PIPELINES;++i)if(bank->pipeline[i])ops->release_pipeline(bank->pipeline[i]);
 }
 memset(bank,0,sizeof(*bank));return 1;
}
int xe_transfer_color_d3d12_init(xe_transfer_color_d3d12_bank *bank,
 const xe_transfer_color_d3d12_config *config,const xe_transfer_color_d3d12_ops *ops)
{
 static const unsigned formats[8]={0,1,2,3,4,5,14,15};
 D3D12_GRAPHICS_PIPELINE_STATE_DESC desc={0};D3D12_INPUT_ELEMENT_DESC input={0};
 XeDxilEdramTransferOptions options={0};XeDxilShaderBinary pixel={0};
 unsigned scale,s,d,converted,index,slot;int success=0;
 if(!bank || bank->device || !config || !ops || !config->device || !config->root ||
  !config->vertex || !config->vertex_bytes || config->enabled_views>255 ||
  !config->scale_x || !config->scale_y || config->scale_x>16 || config->scale_y>16 ||
  !ops->create_pipeline || !ops->release_pipeline || !ops->emit || !ops->free_shader)return 0;
 bank->device=config->device;bank->enabled_views=config->enabled_views;
 bank->scale_classes=config->scale_x==1 && config->scale_y==1?1:4;
 input.SemanticName="POSITION";input.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;
 input.InputSlotClass=D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA;input.InstanceDataStepRate=1;
 desc.pRootSignature=config->root;desc.VS.pShaderBytecode=config->vertex;desc.VS.BytecodeLength=config->vertex_bytes;
 desc.InputLayout.NumElements=1;desc.InputLayout.pInputElementDescs=&input;
 desc.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
 desc.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;desc.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
 desc.RasterizerState.DepthClipEnable=TRUE;desc.SampleMask=UINT32_MAX;desc.SampleDesc.Count=1;
 desc.BlendState.RenderTarget[0].RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;desc.NumRenderTargets=1;
 options.mode=1;options.scale_x=config->scale_x;options.scale_y=config->scale_y;options.source_space=config->source_space;
 for(scale=0;scale<bank->scale_classes;++scale)
 for(s=0;s<8;++s)if(config->enabled_views&(1u<<s))
 for(d=0;d<8;++d)if(config->enabled_views&(1u<<d))
 for(converted=0;converted<2;++converted){
  if(!color_index(s,d,converted,&index))continue;
  options.source_format=formats[s];options.destination_format=formats[d];
  options.source_native=scale&1;options.destination_native=scale>>1;
  options.source_uint=s>=4;options.destination_uint=d>=4;options.value_convert=converted;
  if(!ops->emit(&options,&pixel) || !pixel.data || !pixel.size || pixel.size>UINT32_MAX)goto done;
  desc.PS.pShaderBytecode=pixel.data;desc.PS.BytecodeLength=pixel.size;desc.RTVFormats[0]=xe_transfer_color_d3d12_view(d);
  slot=scale*XE_TRANSFER_COLOR_D3D12_PAIRS+index;
  XE_DRAW_EVENT(XE_DRAW_UTILITY_PIPELINE,XE_DRAW_GRAPHICS_PSO);
  if(FAILED(ops->create_pipeline(bank->device,&desc,bank->pipeline+slot)) || !bank->pipeline[slot])goto done;
  ops->free_shader(&pixel);
 }
 bank->ready=1;success=1;
done:
 ops->free_shader(&pixel);
 if(!success)xe_transfer_color_d3d12_close(bank,ops);
 return success;
}
