/* Serial test initialization only; no dependency from the core to this ABI. */
#define COBJMACROS

#include "transfer_uint_d3d12_revision.h"
#include "libretro/core_transfer_uint.h"
#include "src/xenia/gpu/dxil/edram_transfer.h"
#ifdef XE_TEST_D3D12_PREPARED_SOURCE
#include "libretro/core_transfer_uint_d3d12.h"
#endif
#include <stdlib.h>
#include <string.h>
typedef struct revision_owner {
 unsigned scale_x,scale_y,native2;
 int (*validate)(void*,void*,size_t);void *validation_context;
#ifdef XE_TEST_D3D12_PREPARED_SOURCE
 xe_transfer_uint_d3d12_bank bank;
#else
 ID3D12RootSignature *root;
 ID3D12PipelineState *pipeline[10][3];
#endif
} revision_owner;
static const unsigned pairs[10][2]={{4,6},{6,4},{5,7},{7,5},{14,4},{4,14},{15,5},{5,15},{4,5},{5,4}};
static int pair_index(const xe_test_d3d12_key *key) {
 unsigned i;if(!key || key->source_native || key->dest_native || key->source_msaa!=key->dest_msaa || key->dest_msaa>2)return -1;
 for(i=0;i<10;++i)if(pairs[i][0]==key->source_format && pairs[i][1]==key->dest_format)return (int)i;
 return -1;
}
static void revision_close(void *opaque) {
 revision_owner *owner=(revision_owner*)opaque;
 if(!owner)return;
#ifdef XE_TEST_D3D12_PREPARED_SOURCE
 xe_transfer_uint_d3d12_close(&owner->bank,xe_transfer_uint_d3d12_native_ops());
#else
 {unsigned p,m;for(p=0;p<10;++p)for(m=0;m<3;++m)if(owner->pipeline[p][m])ID3D12PipelineState_Release(owner->pipeline[p][m]);}
 if(owner->root)ID3D12RootSignature_Release(owner->root);
#endif
 free(owner);
}
#ifdef XE_TEST_D3D12_PREPARED_SOURCE
/* The harness serializes startup. No callback or context survives open(). */
static revision_owner *initializing;
static int checked_emit(const xe_transfer_uint_options *options,const xe_transfer_address_key *key,
 unsigned format,XeDxilShaderBinary *out) {
 if(!initializing || !xe_dxil_transfer_uint_shader_shape(options,key,format,out))return 0;
 return initializing->validate(initializing->validation_context,out->data,out->size);
}
#endif
static void *revision_open(const xe_test_d3d12_config *input) {
 revision_owner *owner;
 if(!input || !input->device || !input->vertex || !input->vertex_bytes || !input->validate || input->native2!=1 ||
    input->scale_x!=input->scale_y || (input->scale_x!=1 && input->scale_x!=2 && input->scale_x!=4))return NULL;
#ifdef XE_TEST_D3D12_PREPARED_SOURCE
 if(initializing)return NULL;
#endif
 owner=(revision_owner*)calloc(1,sizeof(*owner));if(!owner)return NULL;
 owner->scale_x=input->scale_x;owner->scale_y=input->scale_y;owner->native2=input->native2;
 owner->validate=input->validate;owner->validation_context=input->validation_context;
#ifdef XE_TEST_D3D12_PREPARED_SOURCE
 {
  xe_transfer_uint_d3d12_config config={0};xe_transfer_uint_d3d12_ops ops=*xe_transfer_uint_d3d12_native_ops();int success;
  config.device=input->device;config.vertex=input->vertex;config.vertex_bytes=input->vertex_bytes;
  config.native2=input->native2;config.scale_x=input->scale_x;config.scale_y=input->scale_y;
  ops.emit=checked_emit;initializing=owner;
  success=xe_transfer_uint_d3d12_init(&owner->bank,&config,&ops);initializing=NULL;
  if(!success){revision_close(owner);return NULL;}
 }
#else
 {
  D3D12_ROOT_SIGNATURE_DESC root={0};D3D12_ROOT_PARAMETER parameters[2]={{0}};D3D12_DESCRIPTOR_RANGE range={0};
  D3D12_GRAPHICS_PIPELINE_STATE_DESC desc={0};D3D12_INPUT_ELEMENT_DESC element={0};
  ID3DBlob *serialized=NULL,*error=NULL;ID3D12RootSignature *created_root=NULL;ID3D12PipelineState *created_pipeline;HRESULT status;unsigned p,m;
  XeDxilEdramTransferOptions options={0};XeDxilShaderBinary pixel={0};
  range.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;range.NumDescriptors=1;
  parameters[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[1].DescriptorTable.NumDescriptorRanges=1;parameters[1].DescriptorTable.pDescriptorRanges=&range;
  parameters[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
  parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[0].Constants.ShaderRegister=1;parameters[0].Constants.RegisterSpace=31;parameters[0].Constants.Num32BitValues=4;
  parameters[1].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
  root.NumParameters=2;root.pParameters=parameters;root.Flags=D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
  status=D3D12SerializeRootSignature(&root,D3D_ROOT_SIGNATURE_VERSION_1,&serialized,&error);
  if(error)ID3D10Blob_Release(error);
  if(SUCCEEDED(status) && serialized)status=ID3D12Device_CreateRootSignature(input->device,0,ID3D10Blob_GetBufferPointer(serialized),ID3D10Blob_GetBufferSize(serialized),&IID_ID3D12RootSignature,(void**)&created_root);
  if(serialized)ID3D10Blob_Release(serialized);
  if(FAILED(status) || !created_root){revision_close(owner);return NULL;}
  owner->root=created_root;
  element.SemanticName="POSITION";element.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;
  element.InputSlotClass=D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA;element.InstanceDataStepRate=1;
  desc.pRootSignature=owner->root;desc.VS.pShaderBytecode=input->vertex;desc.VS.BytecodeLength=input->vertex_bytes;
  desc.InputLayout.NumElements=1;desc.InputLayout.pInputElementDescs=&element;desc.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  desc.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;desc.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;desc.RasterizerState.DepthClipEnable=TRUE;
  desc.BlendState.RenderTarget[0].RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;desc.NumRenderTargets=1;desc.SampleMask=UINT32_MAX;
  options.mode=1;options.source_uint=options.destination_uint=1;options.msaa_2x_supported=1;options.scale_x=input->scale_x;options.scale_y=input->scale_y;
  for(p=0;p<10;++p)for(m=0;m<3;++m) {
   options.source_format=pairs[p][0];options.destination_format=pairs[p][1];options.source_msaa=options.destination_msaa=m;
   if(!xe_dxil_build_edram_transfer(&options,&pixel))goto fail;
   if(!owner->validate(owner->validation_context,pixel.data,pixel.size))goto fail;
   desc.PS.pShaderBytecode=pixel.data;desc.PS.BytecodeLength=pixel.size;
   desc.RTVFormats[0]=pairs[p][1]==5 || pairs[p][1]==7?DXGI_FORMAT_R16G16B16A16_UINT:pairs[p][1]==14?DXGI_FORMAT_R32_UINT:pairs[p][1]==15?DXGI_FORMAT_R32G32_UINT:DXGI_FORMAT_R16G16_UINT;
   desc.SampleDesc.Count=1u<<m;
   created_pipeline=NULL;status=ID3D12Device_CreateGraphicsPipelineState(input->device,&desc,&IID_ID3D12PipelineState,(void**)&created_pipeline);
   if(FAILED(status) || !created_pipeline)goto fail;
   owner->pipeline[p][m]=created_pipeline;
   xe_dxil_shader_binary_free(&pixel);
  }
  owner->validate=NULL;owner->validation_context=NULL;return owner;
fail:
  xe_dxil_shader_binary_free(&pixel);revision_close(owner);return NULL;
 }
#endif
#ifdef XE_TEST_D3D12_PREPARED_SOURCE
 owner->validate=NULL;owner->validation_context=NULL;return owner;
#endif
}
static ID3D12RootSignature *revision_root(void *opaque) {
 revision_owner *owner=(revision_owner*)opaque;if(!owner)return NULL;
#ifdef XE_TEST_D3D12_PREPARED_SOURCE
 return owner->bank.ready?owner->bank.root:NULL;
#else
 return owner->root;
#endif
}
static ID3D12PipelineState *revision_pipeline(void *opaque,const xe_test_d3d12_key *key) {
 revision_owner *owner=(revision_owner*)opaque;int p=pair_index(key);if(!owner || p<0)return NULL;
#ifdef XE_TEST_D3D12_PREPARED_SOURCE
 return xe_transfer_uint_d3d12_get(&owner->bank,key->dest_format,key->dest_msaa,key->source_msaa,0,0);
#else
 return owner->pipeline[p][key->dest_msaa];
#endif
}
static int revision_packet(void *opaque,const xe_test_d3d12_key *key,void *out,size_t bytes) {
 revision_owner *owner=(revision_owner*)opaque;xe_transfer_address_key address={0};xe_transfer_uint_options options={0};
#ifndef XE_TEST_D3D12_PREPARED_SOURCE
 xe_transfer_address_config checked;
#endif
 if(!owner || !out || pair_index(key)<0)return 0;
 address.source_color=address.dest_color=1;address.source_msaa=key->source_msaa;address.dest_msaa=key->dest_msaa;
 address.source_native2=address.dest_native2=owner->native2;
 address.source_scale_x=address.dest_scale_x=owner->scale_x;address.source_scale_y=address.dest_scale_y=owner->scale_y;
 address.source_wide=key->source_format==5 || key->source_format==7 || key->source_format==15;
 address.dest_wide=key->dest_format==5 || key->dest_format==7 || key->dest_format==15;
 address.source_pitch=key->source_pitch;address.dest_pitch=key->dest_pitch;address.dest_to_source_base=key->delta;
 options.source_multisampled=key->source_msaa!=0;options.sample_mode=key->dest_msaa?XE_TRANSFER_UINT_SAMPLE_BUILTIN:XE_TRANSFER_UINT_SINGLE_SAMPLE;
#ifdef XE_TEST_D3D12_PREPARED_SOURCE
 if(bytes!=sizeof(xe_transfer_uint_packet))return 0;
 return xe_transfer_uint_packet_prepare((xe_transfer_uint_packet*)out,&options,&address,key->source_format,key->dest_format,0);
#else
 (void)options;
 if(bytes!=4 || !xe_transfer_address_prepare(&checked,&address))return 0;
 *(uint32_t*)out=key->dest_pitch|(key->source_pitch<<10)|(((uint32_t)key->delta&4095)<<20);return 1;
#endif
}
__declspec(dllexport) const xe_test_d3d12_api *xe_test_d3d12_revision_get(unsigned version) {
 static const xe_test_d3d12_api api={XE_TEST_D3D12_ABI,sizeof(xe_test_d3d12_api),sizeof(xe_test_d3d12_config),sizeof(xe_test_d3d12_key),
#ifdef XE_TEST_D3D12_PREPARED_SOURCE
 XE_TEST_D3D12_MAX_PACKET,XE_TEST_D3D12_PREPARED,1,0,
#else
 4,XE_TEST_D3D12_LEGACY,0,1,
#endif
 revision_open,revision_root,revision_pipeline,revision_packet,revision_close};
 return version==XE_TEST_D3D12_ABI && xe_test_d3d12_api_valid(&api)?&api:NULL;
}
