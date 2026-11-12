/* Actual parent/bank comparison through normal typed MSAA resources. BSD; C89. */
#define XE_TRANSFER_IMAGE_IDENTITY_MS
#include "transfer_uint_d3d12_image.h"
#include "transfer_identity_msaa_cases.h"
#include "transfer_test_adapter.h"
#include "libretro/core_transfer_identity_d3d12.h"
#include <d3d12sdklayers.h>
/* Test-only SDK validation; no core logging or API interception. */
static HRESULT identity_debug_enable(int enabled) {
 ID3D12Debug *debug=NULL;HRESULT result;
 if(!enabled)return S_OK;
 result=D3D12GetDebugInterface(&IID_ID3D12Debug,(void**)&debug);
 if(FAILED(result))return result;
 ID3D12Debug_EnableDebugLayer(debug);ID3D12Debug_Release(debug);
 fprintf(stderr,"IDENTITY_D3D12_API_VALIDATION enabled\n");return S_OK;
}
static HRESULT identity_debug_errors(ID3D12Device *device,int enabled) {
 ID3D12InfoQueue *queue=NULL;UINT64 i,count;SIZE_T bytes;HRESULT status,result=S_OK;
 union {void *alignment;unsigned char data[4096];} storage;D3D12_MESSAGE *message;
 if(!enabled)return S_OK;
 status=ID3D12Device_QueryInterface(device,&IID_ID3D12InfoQueue,(void**)&queue);
 if(FAILED(status))return status;
 count=ID3D12InfoQueue_GetNumStoredMessages(queue);
 for(i=0;i<count;++i) {
  bytes=sizeof(storage);status=ID3D12InfoQueue_GetMessage(queue,i,(D3D12_MESSAGE*)storage.data,&bytes);
  if(FAILED(status)){result=status;break;}
  message=(D3D12_MESSAGE*)storage.data;
  if(message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR) {
   fprintf(stderr,"D3D12 API validation error %u: %s\n",(unsigned)message->ID,message->pDescription);result=E_FAIL;
  }
 }
 ID3D12InfoQueue_Release(queue);return result;
}
typedef struct blob {void *data;size_t bytes;} blob;
typedef struct bank {
 ID3D12RootSignature *utility_root,*legacy_root;
 ID3D12PipelineState *legacy[2][4][3][3][3];
 xe_transfer_identity_d3d12_bank production[2][4];
 ID3D12PipelineState *fill[4][3][4],*probe[2];
} bank;
static unsigned sample_count(unsigned index){return 1u<<index;}
static unsigned format_index(unsigned f){return f;}
static HRESULT make_root(ID3D12Device *device,ID3D12RootSignature **out) {
 D3D12_ROOT_SIGNATURE_DESC desc;D3D12_ROOT_PARAMETER parameters[3];D3D12_DESCRIPTOR_RANGE range;
 ID3DBlob *serialized=NULL,*error=NULL;HRESULT status;
 memset(&desc,0,sizeof(desc));memset(parameters,0,sizeof(parameters));memset(&range,0,sizeof(range));
 range.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;range.NumDescriptors=1;
 parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;parameters[0].DescriptorTable.NumDescriptorRanges=1;parameters[0].DescriptorTable.pDescriptorRanges=&range;
 parameters[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;
 parameters[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;parameters[2].Constants.Num32BitValues=4;desc.NumParameters=3;
 desc.pParameters=parameters;desc.Flags=D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
 status=D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&serialized,&error);
 if(SUCCEEDED(status))status=ID3D12Device_CreateRootSignature(device,0,ID3D10Blob_GetBufferPointer(serialized),ID3D10Blob_GetBufferSize(serialized),&IID_ID3D12RootSignature,(void**)out);
 if(error)ID3D10Blob_Release(error);
 if(serialized)ID3D10Blob_Release(serialized);
 return status;
}
#include "transfer_identity_msaa_reference_d3d12_bank.h"
static DXGI_FORMAT numeric_format(unsigned view) {
 static const DXGI_FORMAT formats[4]={DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R16G16B16A16_UNORM,DXGI_FORMAT_R10G10B10A2_UNORM,DXGI_FORMAT_R16G16B16A16_FLOAT};
 return formats[view];
}
#include "transfer_identity_msaa_legacy_d3d12.h"
static HRESULT make_bank(ID3D12Device *device,bank *b,const char *prefix,int native2) {
 blob vs={0},fill={0},probe[2]={{0},{0}};
 D3D12_GRAPHICS_PIPELINE_STATE_DESC desc;D3D12_COMPUTE_PIPELINE_STATE_DESC compute;D3D12_INPUT_ELEMENT_DESC input;
 char path[4096];unsigned source,f,c,s;HRESULT status=E_FAIL;
 sprintf(path,"%s-vertex.dxil",prefix);vs.data=read_binary(path,&vs.bytes);
 sprintf(path,"%s-fill.dxil",prefix);fill.data=read_binary(path,&fill.bytes);if(!vs.data || !fill.data)goto done;
 for(source=0;source<2;++source) {
  sprintf(path,"%s-probe-s%u.dxil",prefix,source);probe[source].data=read_binary(path,&probe[source].bytes);if(!probe[source].data)goto done;
 }

 status=make_root(device,&b->utility_root);if(FAILED(status))goto done;
 memset(&desc,0,sizeof(desc));memset(&input,0,sizeof(input));desc.VS.pShaderBytecode=vs.data;desc.VS.BytecodeLength=vs.bytes;
 input.SemanticName="POSITION";input.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;input.InputSlotClass=D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA;input.InstanceDataStepRate=1;
 desc.InputLayout.NumElements=1;desc.InputLayout.pInputElementDescs=&input;desc.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
 desc.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;desc.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;desc.RasterizerState.DepthClipEnable=TRUE;desc.BlendState.RenderTarget[0].RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;desc.NumRenderTargets=1;
 for(f=0;f<4;++f)for(c=0;c<3;++c) {
  if(c==1 && !native2)continue;
  desc.RTVFormats[0]=format(f);desc.SampleDesc.Count=sample_count(c);desc.pRootSignature=b->utility_root;desc.PS.pShaderBytecode=fill.data;desc.PS.BytecodeLength=fill.bytes;
  for(s=0;s<sample_count(c);++s){desc.SampleMask=1u<<s;status=ID3D12Device_CreateGraphicsPipelineState(device,&desc,&IID_ID3D12PipelineState,(void**)&b->fill[f][c][s]);if(FAILED(status))goto done;}

 }
 status=make_identity_bank(device,b,native2,&desc.VS);if(FAILED(status))goto done;
 status=make_legacy_bank(device,b,getenv("IDENTITY_LEGACY_PREFIX"),native2,&desc.VS);if(FAILED(status))goto done;
 memset(&compute,0,sizeof(compute));compute.pRootSignature=b->utility_root;
 for(source=0;source<2;++source){compute.CS.pShaderBytecode=probe[source].data;compute.CS.BytecodeLength=probe[source].bytes;status=ID3D12Device_CreateComputePipelineState(device,&compute,&IID_ID3D12PipelineState,(void**)&b->probe[source]);if(FAILED(status))goto done;}
 status=S_OK;
done:
 free(vs.data);free(fill.data);for(source=0;source<2;++source)free(probe[source].data);return status;
}
static void close_bank(bank *b) {
 unsigned f,c,s;
 close_legacy_bank(b);close_identity_bank(b);
 for(f=0;f<4;++f)for(c=0;c<3;++c)for(s=0;s<4;++s)if(b->fill[f][c][s])ID3D12PipelineState_Release(b->fill[f][c][s]);
 for(s=0;s<2;++s)if(b->probe[s])ID3D12PipelineState_Release(b->probe[s]);

 if(b->utility_root)ID3D12RootSignature_Release(b->utility_root);
}
static HRESULT make_probe_buffer(ID3D12Device *device,UINT64 bytes,ID3D12Resource **out) {
 D3D12_HEAP_PROPERTIES heap;D3D12_RESOURCE_DESC desc;memset(&heap,0,sizeof(heap));heap.Type=D3D12_HEAP_TYPE_DEFAULT;memset(&desc,0,sizeof(desc));desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=bytes;desc.Height=desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
 return ID3D12Device_CreateCommittedResource(device,&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,NULL,&IID_ID3D12Resource,(void**)out);
}
/* Typed images are copied only by this untimed oracle, never production. */
static HRESULT numeric_samples(ID3D12Device *device,unsigned view,unsigned w,unsigned h,
 unsigned samples,ID3D12Resource **out) {
 D3D12_HEAP_PROPERTIES heap;D3D12_RESOURCE_DESC desc;
 memset(&heap,0,sizeof(heap));heap.Type=D3D12_HEAP_TYPE_DEFAULT;memset(&desc,0,sizeof(desc));
 desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=w;desc.Height=h;
 desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=samples;
 desc.Format=numeric_format(view);desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
 return ID3D12Device_CreateCommittedResource(device,&heap,D3D12_HEAP_FLAG_NONE,&desc,
  D3D12_RESOURCE_STATE_COPY_DEST,NULL,&IID_ID3D12Resource,(void**)out);
}
int main(int argc,char **argv) {
 static const unsigned formats[]={0,1,2,3};
 static const unsigned scales[3][4]={{1,1,1,1},{2,2,2,2},{4,4,4,4}};
 enum {BYTES=320*64*4*16+256};
 IDXGIFactory4 *factory=NULL;IDXGIAdapter *adapter=NULL;ID3D12Device *device=NULL;DXGI_ADAPTER_DESC adapter_desc;
 ID3D12CommandQueue *queue=NULL;ID3D12CommandAllocator *allocator=NULL;ID3D12GraphicsCommandList *list=NULL;ID3D12CommandList *submitted;
 ID3D12Resource *guard=NULL,*vertices=NULL,*readback=NULL,*gpu_buffer=NULL,*source=NULL,*dest=NULL;ID3D12DescriptorHeap *srv_heap=NULL,*rtv_heap=NULL;ID3D12Fence *fence=NULL;HANDLE event=NULL;
 D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS levels;D3D12_DESCRIPTOR_HEAP_DESC heap;D3D12_SHADER_RESOURCE_VIEW_DESC srv;
 D3D12_CPU_DESCRIPTOR_HANDLE srv_cpu[3],rtv[3];D3D12_GPU_DESCRIPTOR_HANDLE srv_gpu[3];D3D12_RENDER_TARGET_VIEW_DESC rtv_desc;D3D12_COMMAND_QUEUE_DESC queue_desc;D3D12_VERTEX_BUFFER_VIEW vertex;D3D12_VIEWPORT viewport;D3D12_RECT scissor;D3D12_RANGE range;
 ID3D12Resource *typed_source=NULL,*typed_dest=NULL;
 bank b={0};xe_transfer_address_key key={0};xe_transfer_identity_options options={0};xe_transfer_identity_packet packet;
 void *guard_data=NULL,*vertex_data=NULL,*readback_data=NULL;float clear[4];
 xe_transfer_identity_d3d12_bank *prepared_owner;ID3D12PipelineState *prepared_pipeline=NULL;
 unsigned active_bytes=BYTES,variant;uint32_t packed;ID3D12RootSignature *active_root;
 uint32_t constants[4],wanted[4],*pixels;
 unsigned f,s,sm,dm,n,c,mode,si,di,source_samples,samples,sw,sh,dw,dh,x,y,lane,physical_sample,index,records,cases=0;HRESULT status=E_FAIL;int native2=1,pending=0,hardware=getenv("MSAA_HARDWARE")!=NULL;
 int debug=getenv("IDENTITY_MSAA_D3D12_DEBUG")!=NULL;
 int typed_resources=1;
 if(argc!=2 || strlen(argv[1])>2048)return 1;
 fprintf(stderr,"IDENTITY_MSAA_CONTROL sample0 scale0 mask0\n");

#define CHECK(expression) do {status=(expression);if(FAILED(status)){fprintf(stderr,"MSAA WARP line%u status%08lx\n",(unsigned)__LINE__,(unsigned long)status);goto done;}}while(0)
 CHECK(identity_debug_enable(debug));
 CHECK(CreateDXGIFactory1(&IID_IDXGIFactory4,(void**)&factory));CHECK(transfer_test_adapter(factory,hardware,&adapter));CHECK(D3D12CreateDevice((IUnknown*)adapter,D3D_FEATURE_LEVEL_11_0,&IID_ID3D12Device,(void**)&device));
 CHECK(IDXGIAdapter_GetDesc(adapter,&adapter_desc));
 fprintf(stderr,"MSAA_D3D12_ADAPTER hardware%d vendor%u device%u subsystem%u revision%u\n",
  hardware,adapter_desc.VendorId,adapter_desc.DeviceId,adapter_desc.SubSysId,adapter_desc.Revision);
 for(f=0;f<4;++f){memset(&levels,0,sizeof(levels));levels.Format=format(f);levels.SampleCount=4;CHECK(ID3D12Device_CheckFeatureSupport(device,D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,&levels,sizeof(levels)));if(!levels.NumQualityLevels){status=E_FAIL;goto done;}levels.SampleCount=2;levels.NumQualityLevels=0;CHECK(ID3D12Device_CheckFeatureSupport(device,D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,&levels,sizeof(levels)));if(!levels.NumQualityLevels)native2=0;}
 for(f=0;f<4;++f){memset(&levels,0,sizeof(levels));levels.Format=numeric_format(f);levels.SampleCount=4;CHECK(ID3D12Device_CheckFeatureSupport(device,D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,&levels,sizeof(levels)));if(!levels.NumQualityLevels){status=E_FAIL;goto done;}levels.SampleCount=2;levels.NumQualityLevels=0;CHECK(ID3D12Device_CheckFeatureSupport(device,D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,&levels,sizeof(levels)));if(!levels.NumQualityLevels)native2=0;}
 fprintf(stderr,"IDENTITY_MSAA_D3D12 hardware%d native2=%d typedResources=%d\n",hardware,native2,typed_resources);CHECK(make_bank(device,&b,argv[1],native2));
 CHECK(buffer(device,D3D12_HEAP_TYPE_UPLOAD,BYTES,&guard));CHECK(buffer(device,D3D12_HEAP_TYPE_UPLOAD,16,&vertices));CHECK(buffer(device,D3D12_HEAP_TYPE_READBACK,BYTES,&readback));CHECK(make_probe_buffer(device,BYTES,&gpu_buffer));range.Begin=range.End=0;CHECK(ID3D12Resource_Map(guard,0,&range,&guard_data));memset(guard_data,0x6d,BYTES);CHECK(ID3D12Resource_Map(vertices,0,&range,&vertex_data));{float *v=(float*)vertex_data;v[0]=v[1]=-1;v[2]=v[3]=1;}
 memset(&heap,0,sizeof(heap));heap.NumDescriptors=3;heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;heap.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;CHECK(ID3D12Device_CreateDescriptorHeap(device,&heap,&IID_ID3D12DescriptorHeap,(void**)&srv_heap));ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(srv_heap,&srv_cpu[0]);ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(srv_heap,&srv_gpu[0]);srv_cpu[1]=srv_cpu[0];srv_cpu[1].ptr+=ID3D12Device_GetDescriptorHandleIncrementSize(device,D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);srv_gpu[1]=srv_gpu[0];srv_gpu[1].ptr+=ID3D12Device_GetDescriptorHandleIncrementSize(device,D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);srv_cpu[2]=srv_cpu[1];srv_cpu[2].ptr+=ID3D12Device_GetDescriptorHandleIncrementSize(device,D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);srv_gpu[2]=srv_gpu[1];srv_gpu[2].ptr+=ID3D12Device_GetDescriptorHandleIncrementSize(device,D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
 heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;heap.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_NONE;CHECK(ID3D12Device_CreateDescriptorHeap(device,&heap,&IID_ID3D12DescriptorHeap,(void**)&rtv_heap));ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(rtv_heap,&rtv[0]);rtv[1]=rtv[0];rtv[1].ptr+=ID3D12Device_GetDescriptorHandleIncrementSize(device,D3D12_DESCRIPTOR_HEAP_TYPE_RTV);rtv[2]=rtv[1];rtv[2].ptr+=ID3D12Device_GetDescriptorHandleIncrementSize(device,D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
 memset(&queue_desc,0,sizeof(queue_desc));CHECK(ID3D12Device_CreateCommandQueue(device,&queue_desc,&IID_ID3D12CommandQueue,(void**)&queue));CHECK(ID3D12Device_CreateCommandAllocator(device,D3D12_COMMAND_LIST_TYPE_DIRECT,&IID_ID3D12CommandAllocator,(void**)&allocator));CHECK(ID3D12Device_CreateCommandList(device,0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator,NULL,&IID_ID3D12GraphicsCommandList,(void**)&list));CHECK(ID3D12GraphicsCommandList_Close(list));CHECK(ID3D12Device_CreateFence(device,0,D3D12_FENCE_FLAG_NONE,&IID_ID3D12Fence,(void**)&fence));event=CreateEventA(NULL,FALSE,FALSE,NULL);if(!event){status=E_FAIL;goto done;}
 vertex.BufferLocation=ID3D12Resource_GetGPUVirtualAddress(vertices);vertex.SizeInBytes=vertex.StrideInBytes=16;key.source_color=key.dest_color=1;key.source_pitch=key.dest_pitch=1;
 for(n=0;n<2;++n)for(s=0;s<4;++s)for(sm=0;sm<3;++sm)for(dm=0;dm<3;++dm)for(c=0;c<3;++c)for(mode=0;mode<2;++mode)for(variant=0;variant<2;++variant) {
  if(n && !native2)continue;
  if(mode)continue;
  prepared_owner=&b.production[n][c];
  active_root=variant?b.legacy_root:prepared_owner->root;
  key.source_msaa=sm;key.dest_msaa=dm;key.source_native2=key.dest_native2=n;key.source_wide=key.dest_wide=0;options.view=s;key.dest_scale_x=scales[c][0];key.dest_scale_y=scales[c][1];key.source_scale_x=scales[c][2];key.source_scale_y=scales[c][3];options.source_multisampled=sm!=0;options.sample_mode=dm?(mode?XE_TRANSFER_IDENTITY_SAMPLE_CONSTANT:XE_TRANSFER_IDENTITY_SAMPLE_BUILTIN):XE_TRANSFER_IDENTITY_SINGLE_SAMPLE;
  if(!xe_transfer_identity_packet_prepare(&packet,&options,&key,formats[s],formats[s],s==1,0)){status=E_FAIL;goto done;}
  sw=(80*key.source_scale_x)>>(sm==2);sh=(16*key.source_scale_y)>>(sm!=0);dw=(80*key.dest_scale_x)>>(dm==2);dh=(16*key.dest_scale_y)>>(dm!=0);si=sm==0?0:sm==1 && n?1:2;di=dm==0?0:dm==1 && n?1:2;source_samples=sample_count(si);samples=sample_count(di);
  CHECK(texture_samples(device,formats[s],sw,sh,1,source_samples,&source));CHECK(texture_samples(device,formats[s],dw,dh,1,samples,&dest));if(typed_resources) {
   CHECK(numeric_samples(device,s,sw,sh,source_samples,&typed_source));
   CHECK(numeric_samples(device,s,dw,dh,samples,&typed_dest));
  }memset(&rtv_desc,0,sizeof(rtv_desc));rtv_desc.Format=format(s);rtv_desc.ViewDimension=si?D3D12_RTV_DIMENSION_TEXTURE2DMS:D3D12_RTV_DIMENSION_TEXTURE2D;ID3D12Device_CreateRenderTargetView(device,source,&rtv_desc,rtv[0]);rtv_desc.ViewDimension=di?D3D12_RTV_DIMENSION_TEXTURE2DMS:D3D12_RTV_DIMENSION_TEXTURE2D;ID3D12Device_CreateRenderTargetView(device,dest,&rtv_desc,rtv[1]);rtv_desc.Format=numeric_format(s);ID3D12Device_CreateRenderTargetView(device,typed_resources?typed_dest:dest,&rtv_desc,rtv[2]);
  memset(&srv,0,sizeof(srv));srv.Format=format(formats[s]);srv.ViewDimension=si?D3D12_SRV_DIMENSION_TEXTURE2DMS:D3D12_SRV_DIMENSION_TEXTURE2D;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;if(!si)srv.Texture2D.MipLevels=1;ID3D12Device_CreateShaderResourceView(device,source,&srv,srv_cpu[0]);srv.Format=numeric_format(s);ID3D12Device_CreateShaderResourceView(device,typed_resources?typed_source:source,&srv,srv_cpu[2]);memset(&srv,0,sizeof(srv));srv.Format=format(formats[s]);srv.ViewDimension=di?D3D12_SRV_DIMENSION_TEXTURE2DMS:D3D12_SRV_DIMENSION_TEXTURE2D;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;if(!di)srv.Texture2D.MipLevels=1;ID3D12Device_CreateShaderResourceView(device,dest,&srv,srv_cpu[1]);
  CHECK(ID3D12CommandAllocator_Reset(allocator));CHECK(ID3D12GraphicsCommandList_Reset(list,allocator,NULL));transition(list,gpu_buffer,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_DEST);ID3D12GraphicsCommandList_CopyBufferRegion(list,gpu_buffer,0,guard,0,active_bytes);transition(list,gpu_buffer,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  for(lane=0;lane<4;++lane)clear[lane]=(float)((7&(lane==3?identity_ms_alpha_mask(s):identity_ms_mask(s)))|identity_ms_base(s));
  ID3D12GraphicsCommandList_ClearRenderTargetView(list,rtv[0],clear,0,NULL);ID3D12GraphicsCommandList_ClearRenderTargetView(list,rtv[1],clear,0,NULL);ID3D12GraphicsCommandList_SetDescriptorHeaps(list,1,&srv_heap);ID3D12GraphicsCommandList_IASetVertexBuffers(list,0,1,&vertex);ID3D12GraphicsCommandList_IASetPrimitiveTopology(list,D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  memset(&viewport,0,sizeof(viewport));viewport.Width=(float)sw;viewport.Height=(float)sh;viewport.MaxDepth=1;scissor.left=scissor.top=0;scissor.right=(LONG)sw;scissor.bottom=(LONG)sh;ID3D12GraphicsCommandList_RSSetViewports(list,1,&viewport);ID3D12GraphicsCommandList_RSSetScissorRects(list,1,&scissor);ID3D12GraphicsCommandList_OMSetRenderTargets(list,1,&rtv[0],FALSE,NULL);ID3D12GraphicsCommandList_SetGraphicsRootSignature(list,b.utility_root);constants[1]=identity_ms_mask(s);constants[2]=identity_ms_alpha_mask(s);constants[3]=identity_ms_base(s);
  for(physical_sample=0;physical_sample<source_samples;++physical_sample){constants[0]=physical_sample;ID3D12GraphicsCommandList_SetPipelineState(list,b.fill[format_index(formats[s])][si][physical_sample]);ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(list,2,4,constants,0);ID3D12GraphicsCommandList_DrawInstanced(list,6,1,0,0);}
  if(typed_resources) {
   transition(list,source,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);
   transition(list,dest,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);
   ID3D12GraphicsCommandList_CopyResource(list,typed_source,source);
   ID3D12GraphicsCommandList_CopyResource(list,typed_dest,dest);
   transition(list,typed_source,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
   transition(list,typed_dest,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_RENDER_TARGET);
  }else transition(list,source,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);viewport.Width=(float)dw;viewport.Height=(float)dh;scissor.right=(LONG)dw;scissor.bottom=(LONG)dh;ID3D12GraphicsCommandList_RSSetViewports(list,1,&viewport);ID3D12GraphicsCommandList_RSSetScissorRects(list,1,&scissor);ID3D12GraphicsCommandList_OMSetRenderTargets(list,1,&rtv[2],FALSE,NULL);
  ID3D12GraphicsCommandList_SetGraphicsRootSignature(list,active_root);
  ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(list,0,srv_gpu[2]);
  for(physical_sample=0;physical_sample<(dm && mode?samples:1);++physical_sample){if(!identity_ms_active(&key,physical_sample))continue;packet.sample=physical_sample;
   prepared_pipeline=xe_transfer_identity_d3d12_get(prepared_owner,formats[s],dm,
    sm,0,0);
   if(variant)prepared_pipeline=b.legacy[n][s][dm][sm][c];
   if(!prepared_pipeline){status=E_FAIL;goto done;}
   ID3D12GraphicsCommandList_SetPipelineState(list,prepared_pipeline);
   if(variant) {
    packed=key.dest_pitch|(key.source_pitch<<10)|(((uint32_t)key.dest_to_source_base&4095)<<20);
    ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(list,1,1,&packed,0);
   }else ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(list,1,5,&packet,0);ID3D12GraphicsCommandList_DrawInstanced(list,6,1,0,0);}
  if(typed_resources) {
   transition(list,typed_dest,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);
   transition(list,dest,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
   ID3D12GraphicsCommandList_CopyResource(list,dest,typed_dest);
   transition(list,dest,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  }else transition(list,dest,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);constants[0]=dw;constants[1]=dh;constants[2]=samples;constants[3]=active_bytes/16;records=dw*dh*samples;ID3D12GraphicsCommandList_SetComputeRootSignature(list,b.utility_root);ID3D12GraphicsCommandList_SetPipelineState(list,b.probe[di!=0]);ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(list,0,srv_gpu[1]);ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(list,1,ID3D12Resource_GetGPUVirtualAddress(gpu_buffer));ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(list,2,4,constants,0);ID3D12GraphicsCommandList_Dispatch(list,(records+63)/64+1,1,1);
  transition(list,gpu_buffer,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);ID3D12GraphicsCommandList_CopyBufferRegion(list,readback,0,gpu_buffer,0,active_bytes);transition(list,gpu_buffer,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);CHECK(ID3D12GraphicsCommandList_Close(list));submitted=(ID3D12CommandList*)list;ID3D12CommandQueue_ExecuteCommandLists(queue,1,&submitted);pending=1;CHECK(ID3D12CommandQueue_Signal(queue,fence,cases+1));CHECK(ID3D12Fence_SetEventOnCompletion(fence,cases+1,event));if(WaitForSingleObject(event,10000)!=WAIT_OBJECT_0){status=E_FAIL;goto done;}pending=0;
  range.Begin=0;range.End=active_bytes;CHECK(ID3D12Resource_Map(readback,0,&range,&readback_data));pixels=(uint32_t*)readback_data;
  for(y=0;y<dh;++y)for(x=0;x<dw;++x)for(physical_sample=0;physical_sample<samples;++physical_sample){identity_ms_expected(&key,s,x,y,physical_sample,wanted);index=((y*dw+x)*samples+physical_sample)*4;for(lane=0;lane<4;++lane)if(pixels[index+lane]!=wanted[lane]){fprintf(stderr,"MS WARP sf%u df%u sm%u dm%u native%u scale%u mode%u pixel%u,%u sample%u lane%u %08x != %08x\n",formats[s],formats[s],sm,dm,n,c,mode,x,y,physical_sample,lane,pixels[index+lane],wanted[lane]);status=E_FAIL;goto done;}}
  for(x=records*4;x<active_bytes/4;++x)if(pixels[x]!=0x6d6d6d6du){status=E_FAIL;goto done;}
  range.Begin=range.End=0;ID3D12Resource_Unmap(readback,0,&range);readback_data=NULL;
  ID3D12Resource_Release(source);ID3D12Resource_Release(dest);source=dest=NULL;
  if(typed_source)ID3D12Resource_Release(typed_source);
  if(typed_dest)ID3D12Resource_Release(typed_dest);
  typed_source=typed_dest=NULL;++cases;
 }
 if(cases!=(native2?432u:216u)){status=E_FAIL;goto done;}
 printf("PASS: %s %u numeric parent/bank MSAA images, all four views/three uniform scales/builtin, native2=%d, inactive samples and probe tails\n",hardware?"hardware D3D12":"WARP",cases,native2);
 status=S_OK;
done:
 if(pending){fprintf(stderr,"Pending MSAA WARP resources retained\n");exit(1);}
 if(readback_data)ID3D12Resource_Unmap(readback,0,NULL);
 if(guard_data)ID3D12Resource_Unmap(guard,0,NULL);
 if(vertex_data)ID3D12Resource_Unmap(vertices,0,NULL);
 if(event)CloseHandle(event);
 if(fence)ID3D12Fence_Release(fence);
 if(list)ID3D12GraphicsCommandList_Release(list);
 if(allocator)ID3D12CommandAllocator_Release(allocator);
 if(queue)ID3D12CommandQueue_Release(queue);
 close_bank(&b);
 if(typed_source)ID3D12Resource_Release(typed_source);
 if(typed_dest)ID3D12Resource_Release(typed_dest);
 if(source)ID3D12Resource_Release(source);
 if(dest)ID3D12Resource_Release(dest);
 if(gpu_buffer)ID3D12Resource_Release(gpu_buffer);
 if(readback)ID3D12Resource_Release(readback);
 if(guard)ID3D12Resource_Release(guard);
 if(vertices)ID3D12Resource_Release(vertices);
 if(srv_heap)ID3D12DescriptorHeap_Release(srv_heap);
 if(rtv_heap)ID3D12DescriptorHeap_Release(rtv_heap);
 if(device){if(FAILED(identity_debug_errors(device,debug)))status=E_FAIL;ID3D12Device_Release(device);}
 if(adapter)IDXGIAdapter_Release(adapter);
 if(factory)IDXGIFactory4_Release(factory);
 return FAILED(status)?1:0;
}
