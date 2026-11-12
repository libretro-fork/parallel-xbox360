/* Independent sample fill -> real transfer -> compute probe. BSD; C89 test. */
#include "transfer_uint_d3d12_image.h"
#include "transfer_msaa_image_cases.h"
#include "transfer_test_adapter.h"
#ifdef XE_TEST_PRODUCTION_UINT_BANK
#include "libretro/core_transfer_uint_d3d12.h"
#endif
typedef struct blob {void *data;size_t bytes;} blob;
typedef struct bank {
 ID3D12RootSignature *transfer_root,*utility_root;
#ifdef XE_TEST_PRODUCTION_UINT_BANK
 xe_transfer_uint_d3d12_bank production[2][4];
#endif
#if defined(XE_TEST_PREPARED_UINT_SHAPES) && !defined(XE_TEST_PRODUCTION_UINT_BANK)
 ID3D12PipelineState *shaped[2][6][4][3][3][6];
#endif
 ID3D12PipelineState *fill[4][3][4],*transfer[4][3][2][6],*probe[2];
} bank;
static unsigned sample_count(unsigned index){return 1u<<index;}
static unsigned format_index(unsigned f){return f==5 || f==7?1:f==14?2:f==15?3:0;}
static HRESULT make_root(ID3D12Device *device,int transfer,ID3D12RootSignature **out) {
 D3D12_ROOT_SIGNATURE_DESC desc;D3D12_ROOT_PARAMETER parameters[3];D3D12_DESCRIPTOR_RANGE range;
 ID3DBlob *serialized=NULL,*error=NULL;HRESULT status;
 memset(&desc,0,sizeof(desc));memset(parameters,0,sizeof(parameters));memset(&range,0,sizeof(range));
 range.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;range.NumDescriptors=1;
 parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;parameters[0].DescriptorTable.NumDescriptorRanges=1;parameters[0].DescriptorTable.pDescriptorRanges=&range;
 if(transfer) {
  parameters[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;parameters[1].Constants.ShaderRegister=1;parameters[1].Constants.RegisterSpace=31;parameters[1].Constants.Num32BitValues=28;desc.NumParameters=2;
 } else {
  parameters[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;
  parameters[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;parameters[2].Constants.Num32BitValues=4;desc.NumParameters=3;
 }
 desc.pParameters=parameters;desc.Flags=D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
 status=D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&serialized,&error);
 if(SUCCEEDED(status))status=ID3D12Device_CreateRootSignature(device,0,ID3D10Blob_GetBufferPointer(serialized),ID3D10Blob_GetBufferSize(serialized),&IID_ID3D12RootSignature,(void**)out);
 if(error)ID3D10Blob_Release(error);
 if(serialized)ID3D10Blob_Release(serialized);
 return status;
}
#ifdef XE_TEST_PREPARED_UINT_SHAPES
/* Borrowed command-line view, valid until main returns. */
static const char *shaped_prefix;
#ifdef XE_TEST_PRODUCTION_UINT_BANK
#include "transfer_uint_production_d3d12_bank.h"
#else
#include "transfer_uint_shape_d3d12_bank.h"
#endif
#endif
#ifdef XE_TEST_D3D12_WARM
#include "transfer_uint_warm_d3d12_profile.h"
#endif
static HRESULT make_bank(ID3D12Device *device,bank *b,const char *prefix,int native2) {
 blob vs={0},fill={0},transfer[2][3]={{{0}}},probe[2]={{0},{0}};
 D3D12_GRAPHICS_PIPELINE_STATE_DESC desc;D3D12_COMPUTE_PIPELINE_STATE_DESC compute;D3D12_INPUT_ELEMENT_DESC input;
 char path[4096];unsigned source,mode,f,c,s;HRESULT status=E_FAIL;
#ifndef XE_TEST_PREPARED_UINT_SHAPES
 unsigned v;
#endif
 sprintf(path,"%s-vertex.dxil",prefix);vs.data=read_binary(path,&vs.bytes);
 sprintf(path,"%s-fill.dxil",prefix);fill.data=read_binary(path,&fill.bytes);if(!vs.data || !fill.data)goto done;
 for(source=0;source<2;++source) {
  sprintf(path,"%s-probe-s%u.dxil",prefix,source);probe[source].data=read_binary(path,&probe[source].bytes);if(!probe[source].data)goto done;
#ifndef XE_TEST_PREPARED_UINT_SHAPES
  for(mode=0;mode<3;++mode){sprintf(path,"%s-s%u-m%u-t0.dxil",prefix,source,mode);transfer[source][mode].data=read_binary(path,&transfer[source][mode].bytes);if(!transfer[source][mode].data)goto done;}
#endif
 }
 status=make_root(device,1,&b->transfer_root);if(FAILED(status))goto done;
 status=make_root(device,0,&b->utility_root);if(FAILED(status))goto done;
 memset(&desc,0,sizeof(desc));memset(&input,0,sizeof(input));desc.VS.pShaderBytecode=vs.data;desc.VS.BytecodeLength=vs.bytes;
 input.SemanticName="POSITION";input.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;input.InputSlotClass=D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA;input.InstanceDataStepRate=1;
 desc.InputLayout.NumElements=1;desc.InputLayout.pInputElementDescs=&input;desc.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
 desc.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;desc.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;desc.RasterizerState.DepthClipEnable=TRUE;desc.BlendState.RenderTarget[0].RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;desc.NumRenderTargets=1;
 for(f=0;f<4;++f)for(c=0;c<3;++c) {
  if(c==1 && !native2)continue;
  desc.RTVFormats[0]=format(f==0?4:f==1?5:f==2?14:15);desc.SampleDesc.Count=sample_count(c);desc.pRootSignature=b->utility_root;desc.PS.pShaderBytecode=fill.data;desc.PS.BytecodeLength=fill.bytes;
  for(s=0;s<sample_count(c);++s){desc.SampleMask=1u<<s;status=ID3D12Device_CreateGraphicsPipelineState(device,&desc,&IID_ID3D12PipelineState,(void**)&b->fill[f][c][s]);if(FAILED(status))goto done;}
  desc.pRootSignature=b->transfer_root;
#ifndef XE_TEST_PREPARED_UINT_SHAPES
  for(source=0;source<2;++source)for(v=0;v<6;++v) {
   if((c==0 && v!=0) || (c!=0 && ((v==1 && c!=2) || (v>=2 && v-2>=sample_count(c)))))continue;
   mode=c==0?0:v<2?1:2;desc.PS.pShaderBytecode=transfer[source][mode].data;desc.PS.BytecodeLength=transfer[source][mode].bytes;desc.SampleMask=c==0?1:v==0?UINT32_MAX:v==1?9:1u<<(v-2);
   status=ID3D12Device_CreateGraphicsPipelineState(device,&desc,&IID_ID3D12PipelineState,(void**)&b->transfer[f][c][source][v]);if(FAILED(status))goto done;
  }
#endif
 }
#ifdef XE_TEST_PREPARED_UINT_SHAPES
 status=make_shaped_bank(device,b,shaped_prefix,native2,&desc);if(FAILED(status))goto done;
#endif
 memset(&compute,0,sizeof(compute));compute.pRootSignature=b->utility_root;
 for(source=0;source<2;++source){compute.CS.pShaderBytecode=probe[source].data;compute.CS.BytecodeLength=probe[source].bytes;status=ID3D12Device_CreateComputePipelineState(device,&compute,&IID_ID3D12PipelineState,(void**)&b->probe[source]);if(FAILED(status))goto done;}
#ifdef XE_TEST_D3D12_WARM
 if(!warm_init(device,&desc)){status=E_FAIL;goto done;}
#endif
 status=S_OK;
done:
 free(vs.data);free(fill.data);for(source=0;source<2;++source){free(probe[source].data);for(mode=0;mode<3;++mode)free(transfer[source][mode].data);}return status;
}
static void close_bank(bank *b) {
 unsigned f,c,s,v;
#ifdef XE_TEST_D3D12_WARM
 warm_close();
#endif
#ifdef XE_TEST_PREPARED_UINT_SHAPES
 close_shaped_bank(b);
#endif
 for(f=0;f<4;++f)for(c=0;c<3;++c){for(s=0;s<4;++s)if(b->fill[f][c][s])ID3D12PipelineState_Release(b->fill[f][c][s]);for(s=0;s<2;++s)for(v=0;v<6;++v)if(b->transfer[f][c][s][v])ID3D12PipelineState_Release(b->transfer[f][c][s][v]);}
 for(s=0;s<2;++s)if(b->probe[s])ID3D12PipelineState_Release(b->probe[s]);
 if(b->transfer_root)ID3D12RootSignature_Release(b->transfer_root);
 if(b->utility_root)ID3D12RootSignature_Release(b->utility_root);
}
static HRESULT make_probe_buffer(ID3D12Device *device,UINT64 bytes,ID3D12Resource **out) {
 D3D12_HEAP_PROPERTIES heap;D3D12_RESOURCE_DESC desc;memset(&heap,0,sizeof(heap));heap.Type=D3D12_HEAP_TYPE_DEFAULT;memset(&desc,0,sizeof(desc));desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=bytes;desc.Height=desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
 return ID3D12Device_CreateCommittedResource(device,&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,NULL,&IID_ID3D12Resource,(void**)out);
}
int main(int argc,char **argv) {
 static const unsigned formats[]={4,5,6,7,14,15};
#ifdef XE_TEST_D3D12_WARM
 static const unsigned scales[6][4]={{1,1,1,1},{2,2,2,2},{4,4,4,4}};
 enum {BYTES=256*1024*1024+256};
#else
 static const unsigned scales[6][4]={{1,1,1,1},{2,2,2,2},{3,5,3,5},{1,1,3,5},{3,5,1,1},{1,1,1,3}};
 enum {BYTES=240*80*4*16+256};
#endif
 IDXGIFactory4 *factory=NULL;IDXGIAdapter *adapter=NULL;ID3D12Device *device=NULL;DXGI_ADAPTER_DESC adapter_desc;
 ID3D12CommandQueue *queue=NULL;ID3D12CommandAllocator *allocator=NULL;ID3D12GraphicsCommandList *list=NULL;ID3D12CommandList *submitted;
 ID3D12Resource *guard=NULL,*vertices=NULL,*readback=NULL,*gpu_buffer=NULL,*source=NULL,*dest=NULL;ID3D12DescriptorHeap *srv_heap=NULL,*rtv_heap=NULL;ID3D12Fence *fence=NULL;HANDLE event=NULL;
 D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS levels;D3D12_DESCRIPTOR_HEAP_DESC heap;D3D12_SHADER_RESOURCE_VIEW_DESC srv;
 D3D12_CPU_DESCRIPTOR_HANDLE srv_cpu[2],rtv[2];D3D12_GPU_DESCRIPTOR_HANDLE srv_gpu[2];D3D12_COMMAND_QUEUE_DESC queue_desc;D3D12_VERTEX_BUFFER_VIEW vertex;D3D12_VIEWPORT viewport;D3D12_RECT scissor;D3D12_RANGE range;
 bank b={0};xe_transfer_address_key key={0};xe_transfer_uint_options options={0};xe_transfer_uint_packet packet;
 void *guard_data=NULL,*vertex_data=NULL,*readback_data=NULL;const float clear[4]={7,7,7,7};
#ifdef XE_TEST_PRODUCTION_UINT_BANK
 xe_transfer_uint_d3d12_bank *prepared_owner;ID3D12PipelineState *prepared_pipeline=NULL;
#endif
#ifdef XE_TEST_D3D12_WARM
 warm_case profiled;int pair_index;LARGE_INTEGER warm_driver;
#endif
 unsigned active_bytes=BYTES;
 uint32_t constants[4],wanted[4],*pixels;
 unsigned f,s,d,sm,dm,n,c,mode,si,di,source_samples,samples,sw,sh,dw,dh,x,y,lane,physical_sample,index,variant,records,cases=0;HRESULT status=E_FAIL;int native2=1,pending=0,hardware=getenv("MSAA_HARDWARE")!=NULL;
#ifdef XE_TEST_PRODUCTION_UINT_BANK
 if(argc!=2 || strlen(argv[1])>2048)return 1;
 shaped_prefix=argv[1];
#elif defined(XE_TEST_PREPARED_UINT_SHAPES)
 if(argc!=3 || strlen(argv[1])>2048 || strlen(argv[2])>2048)return 1;
 shaped_prefix=argv[2];
#else
 if(argc!=2 || strlen(argv[1])>2048)return 1;
#endif
#ifdef XE_TEST_D3D12_WARM
 if(!hardware)return 1;
#endif
#define CHECK(expression) do {status=(expression);if(FAILED(status)){fprintf(stderr,"MSAA WARP line%u status%08lx\n",(unsigned)__LINE__,(unsigned long)status);goto done;}}while(0)
 CHECK(CreateDXGIFactory1(&IID_IDXGIFactory4,(void**)&factory));CHECK(transfer_test_adapter(factory,hardware,&adapter));CHECK(D3D12CreateDevice((IUnknown*)adapter,D3D_FEATURE_LEVEL_11_0,&IID_ID3D12Device,(void**)&device));
 CHECK(IDXGIAdapter_GetDesc(adapter,&adapter_desc));
 fprintf(stderr,"MSAA_D3D12_ADAPTER hardware%d vendor%u device%u subsystem%u revision%u\n",
  hardware,adapter_desc.VendorId,adapter_desc.DeviceId,adapter_desc.SubSysId,adapter_desc.Revision);
 for(f=0;f<4;++f){memset(&levels,0,sizeof(levels));levels.Format=format(f==0?4:f==1?5:f==2?14:15);levels.SampleCount=4;CHECK(ID3D12Device_CheckFeatureSupport(device,D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,&levels,sizeof(levels)));if(!levels.NumQualityLevels){status=E_FAIL;goto done;}levels.SampleCount=2;levels.NumQualityLevels=0;CHECK(ID3D12Device_CheckFeatureSupport(device,D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,&levels,sizeof(levels)));if(!levels.NumQualityLevels)native2=0;}
#ifdef XE_TEST_D3D12_WARM
 CHECK(IDXGIAdapter_CheckInterfaceSupport(adapter,&IID_IDXGIDevice,&warm_driver));
 fprintf(stderr,"D3D12_WARM_DRIVER %08lx:%08lx\n",(unsigned long)warm_driver.HighPart,(unsigned long)warm_driver.LowPart);
 fprintf(stderr,"D3D12_WARM_ID luid%08lx:%08lx dynamic_control%d reverse%d\n",(unsigned long)adapter_desc.AdapterLuid.HighPart,(unsigned long)adapter_desc.AdapterLuid.LowPart,getenv("TRANSFER_WARM_DYNAMIC_CONTROL")!=NULL,getenv("TRANSFER_REVERSE_ORDER")!=NULL);
#endif
 fprintf(stderr,"MSAA_D3D12 hardware%d native2=%d\n",hardware,native2);CHECK(make_bank(device,&b,argv[1],native2));
 CHECK(buffer(device,D3D12_HEAP_TYPE_UPLOAD,BYTES,&guard));CHECK(buffer(device,D3D12_HEAP_TYPE_UPLOAD,16,&vertices));CHECK(buffer(device,D3D12_HEAP_TYPE_READBACK,BYTES,&readback));CHECK(make_probe_buffer(device,BYTES,&gpu_buffer));range.Begin=range.End=0;CHECK(ID3D12Resource_Map(guard,0,&range,&guard_data));memset(guard_data,0x6d,BYTES);CHECK(ID3D12Resource_Map(vertices,0,&range,&vertex_data));{float *v=(float*)vertex_data;v[0]=v[1]=-1;v[2]=v[3]=1;}
 memset(&heap,0,sizeof(heap));heap.NumDescriptors=2;heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;heap.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;CHECK(ID3D12Device_CreateDescriptorHeap(device,&heap,&IID_ID3D12DescriptorHeap,(void**)&srv_heap));ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(srv_heap,&srv_cpu[0]);ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(srv_heap,&srv_gpu[0]);srv_cpu[1]=srv_cpu[0];srv_cpu[1].ptr+=ID3D12Device_GetDescriptorHandleIncrementSize(device,D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);srv_gpu[1]=srv_gpu[0];srv_gpu[1].ptr+=ID3D12Device_GetDescriptorHandleIncrementSize(device,D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
 heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;heap.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_NONE;CHECK(ID3D12Device_CreateDescriptorHeap(device,&heap,&IID_ID3D12DescriptorHeap,(void**)&rtv_heap));ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(rtv_heap,&rtv[0]);rtv[1]=rtv[0];rtv[1].ptr+=ID3D12Device_GetDescriptorHandleIncrementSize(device,D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
 memset(&queue_desc,0,sizeof(queue_desc));CHECK(ID3D12Device_CreateCommandQueue(device,&queue_desc,&IID_ID3D12CommandQueue,(void**)&queue));CHECK(ID3D12Device_CreateCommandAllocator(device,D3D12_COMMAND_LIST_TYPE_DIRECT,&IID_ID3D12CommandAllocator,(void**)&allocator));CHECK(ID3D12Device_CreateCommandList(device,0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator,NULL,&IID_ID3D12GraphicsCommandList,(void**)&list));CHECK(ID3D12GraphicsCommandList_Close(list));CHECK(ID3D12Device_CreateFence(device,0,D3D12_FENCE_FLAG_NONE,&IID_ID3D12Fence,(void**)&fence));event=CreateEventA(NULL,FALSE,FALSE,NULL);if(!event){status=E_FAIL;goto done;}
 vertex.BufferLocation=ID3D12Resource_GetGPUVirtualAddress(vertices);vertex.SizeInBytes=vertex.StrideInBytes=16;key.source_color=key.dest_color=1;key.source_pitch=key.dest_pitch=1;
#ifdef XE_TEST_D3D12_WARM
 key.source_pitch=33;key.dest_pitch=32;key.dest_to_source_base=1;
#endif
 for(n=0;n<2;++n)for(s=0;s<6;++s)for(d=0;d<6;++d)for(sm=0;sm<3;++sm)for(dm=0;dm<3;++dm)for(c=0;c<6;++c)for(mode=0;mode<2;++mode) {
  if(n && !native2)continue;
#ifdef XE_TEST_D3D12_WARM
  pair_index=warm_pair(formats[s],formats[d]);if(n!=1 || mode || sm!=dm || c>2 || pair_index<0)continue;
#endif
#ifdef XE_TEST_PRODUCTION_UINT_BANK
  if(mode)continue;
  prepared_owner=&b.production[n][c==0?0:c==1?1:c==5?3:2];
#endif
  key.source_msaa=sm;key.dest_msaa=dm;key.source_native2=key.dest_native2=n;key.source_wide=formats[s]==5 || formats[s]==7 || formats[s]==15;key.dest_wide=formats[d]==5 || formats[d]==7 || formats[d]==15;key.dest_scale_x=scales[c][0];key.dest_scale_y=scales[c][1];key.source_scale_x=scales[c][2];key.source_scale_y=scales[c][3];options.source_multisampled=sm!=0;options.sample_mode=dm?(mode?XE_TRANSFER_UINT_SAMPLE_CONSTANT:XE_TRANSFER_UINT_SAMPLE_BUILTIN):XE_TRANSFER_UINT_SINGLE_SAMPLE;
  if(!xe_transfer_uint_packet_prepare(&packet,&options,&key,formats[s],formats[d],0)){status=E_FAIL;goto done;}
  sw=packet.address.word[XE_ADDR_S_TW];sh=packet.address.word[XE_ADDR_S_TH];dw=packet.address.word[XE_ADDR_D_TW];dh=packet.address.word[XE_ADDR_D_TH];si=sm==0?0:sm==1 && n?1:2;di=dm==0?0:dm==1 && n?1:2;source_samples=sample_count(si);samples=sample_count(di);
#ifdef XE_TEST_D3D12_WARM
  sw*=33;sh*=16;dw*=32;dh*=16;active_bytes=dw*dh*samples*16+256;
  if(active_bytes>BYTES){status=E_FAIL;goto done;}
#endif
  CHECK(texture_samples(device,formats[s],sw,sh,1,source_samples,&source));CHECK(texture_samples(device,formats[d],dw,dh,1,samples,&dest));ID3D12Device_CreateRenderTargetView(device,source,NULL,rtv[0]);ID3D12Device_CreateRenderTargetView(device,dest,NULL,rtv[1]);
  memset(&srv,0,sizeof(srv));srv.Format=format(formats[s]);srv.ViewDimension=si?D3D12_SRV_DIMENSION_TEXTURE2DMS:D3D12_SRV_DIMENSION_TEXTURE2D;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;if(!si)srv.Texture2D.MipLevels=1;ID3D12Device_CreateShaderResourceView(device,source,&srv,srv_cpu[0]);memset(&srv,0,sizeof(srv));srv.Format=format(formats[d]);srv.ViewDimension=di?D3D12_SRV_DIMENSION_TEXTURE2DMS:D3D12_SRV_DIMENSION_TEXTURE2D;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;if(!di)srv.Texture2D.MipLevels=1;ID3D12Device_CreateShaderResourceView(device,dest,&srv,srv_cpu[1]);
  CHECK(ID3D12CommandAllocator_Reset(allocator));CHECK(ID3D12GraphicsCommandList_Reset(list,allocator,NULL));transition(list,gpu_buffer,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_DEST);ID3D12GraphicsCommandList_CopyBufferRegion(list,gpu_buffer,0,guard,0,active_bytes);transition(list,gpu_buffer,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  ID3D12GraphicsCommandList_ClearRenderTargetView(list,rtv[0],clear,0,NULL);ID3D12GraphicsCommandList_ClearRenderTargetView(list,rtv[1],clear,0,NULL);ID3D12GraphicsCommandList_SetDescriptorHeaps(list,1,&srv_heap);ID3D12GraphicsCommandList_IASetVertexBuffers(list,0,1,&vertex);ID3D12GraphicsCommandList_IASetPrimitiveTopology(list,D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  memset(&viewport,0,sizeof(viewport));viewport.Width=(float)sw;viewport.Height=(float)sh;viewport.MaxDepth=1;scissor.left=scissor.top=0;scissor.right=(LONG)sw;scissor.bottom=(LONG)sh;ID3D12GraphicsCommandList_RSSetViewports(list,1,&viewport);ID3D12GraphicsCommandList_RSSetScissorRects(list,1,&scissor);ID3D12GraphicsCommandList_OMSetRenderTargets(list,1,&rtv[0],FALSE,NULL);ID3D12GraphicsCommandList_SetGraphicsRootSignature(list,b.utility_root);constants[1]=image_halves(formats[s])?65535:UINT32_MAX;constants[2]=constants[3]=0;
  for(physical_sample=0;physical_sample<source_samples;++physical_sample){constants[0]=physical_sample;ID3D12GraphicsCommandList_SetPipelineState(list,b.fill[format_index(formats[s])][si][physical_sample]);ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(list,2,4,constants,0);ID3D12GraphicsCommandList_DrawInstanced(list,6,1,0,0);}
  transition(list,source,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);viewport.Width=(float)dw;viewport.Height=(float)dh;scissor.right=(LONG)dw;scissor.bottom=(LONG)dh;ID3D12GraphicsCommandList_RSSetViewports(list,1,&viewport);ID3D12GraphicsCommandList_RSSetScissorRects(list,1,&scissor);ID3D12GraphicsCommandList_OMSetRenderTargets(list,1,&rtv[1],FALSE,NULL);
#ifdef XE_TEST_PRODUCTION_UINT_BANK
  ID3D12GraphicsCommandList_SetGraphicsRootSignature(list,prepared_owner->root);
#else
  ID3D12GraphicsCommandList_SetGraphicsRootSignature(list,b.transfer_root);
#endif
  ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(list,0,srv_gpu[0]);
  for(physical_sample=0;physical_sample<(dm && mode?samples:1);++physical_sample){if(!image_ms_active(&key,physical_sample))continue;packet.sample=physical_sample;variant=dm==0?0:mode?physical_sample+2:dm==1 && !n?1:0;
#ifdef XE_TEST_PRODUCTION_UINT_BANK
   prepared_pipeline=xe_transfer_uint_d3d12_get(prepared_owner,formats[d],dm,sm,c==3 || c==5,c==4);
   if(!prepared_pipeline){status=E_FAIL;goto done;}
   ID3D12GraphicsCommandList_SetPipelineState(list,prepared_pipeline);
#elif defined(XE_TEST_PREPARED_UINT_SHAPES)
   ID3D12GraphicsCommandList_SetPipelineState(list,b.shaped[n][c][format_index(formats[d])][dm][sm][variant]);
#else
   ID3D12GraphicsCommandList_SetPipelineState(list,b.transfer[format_index(formats[d])][di][sm!=0][variant]);
#endif
   ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(list,1,28,&packet,0);ID3D12GraphicsCommandList_DrawInstanced(list,6,1,0,0);}
  transition(list,dest,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);constants[0]=dw;constants[1]=dh;constants[2]=samples;constants[3]=active_bytes/16;records=dw*dh*samples;ID3D12GraphicsCommandList_SetComputeRootSignature(list,b.utility_root);ID3D12GraphicsCommandList_SetPipelineState(list,b.probe[di!=0]);ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(list,0,srv_gpu[1]);ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(list,1,ID3D12Resource_GetGPUVirtualAddress(gpu_buffer));ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(list,2,4,constants,0);ID3D12GraphicsCommandList_Dispatch(list,(records+63)/64+1,1,1);
  transition(list,gpu_buffer,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);ID3D12GraphicsCommandList_CopyBufferRegion(list,readback,0,gpu_buffer,0,active_bytes);transition(list,gpu_buffer,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);CHECK(ID3D12GraphicsCommandList_Close(list));submitted=(ID3D12CommandList*)list;ID3D12CommandQueue_ExecuteCommandLists(queue,1,&submitted);pending=1;CHECK(ID3D12CommandQueue_Signal(queue,fence,cases+1));CHECK(ID3D12Fence_SetEventOnCompletion(fence,cases+1,event));if(WaitForSingleObject(event,10000)!=WAIT_OBJECT_0){status=E_FAIL;goto done;}pending=0;
  range.Begin=0;range.End=active_bytes;CHECK(ID3D12Resource_Map(readback,0,&range,&readback_data));pixels=(uint32_t*)readback_data;
  for(y=0;y<dh;++y)for(x=0;x<dw;++x)for(physical_sample=0;physical_sample<samples;++physical_sample){image_ms_expected(&key,formats[s],formats[d],x,y,physical_sample,wanted);index=((y*dw+x)*samples+physical_sample)*4;for(lane=0;lane<image_components(formats[d]);++lane)if(pixels[index+lane]!=wanted[lane]){fprintf(stderr,"MS WARP sf%u df%u sm%u dm%u native%u scale%u mode%u pixel%u,%u sample%u lane%u %08x != %08x\n",formats[s],formats[d],sm,dm,n,c,mode,x,y,physical_sample,lane,pixels[index+lane],wanted[lane]);status=E_FAIL;goto done;}}
  for(x=records*4;x<active_bytes/4;++x)if(pixels[x]!=0x6d6d6d6du){status=E_FAIL;goto done;}
  range.Begin=range.End=0;ID3D12Resource_Unmap(readback,0,&range);readback_data=NULL;
#ifdef XE_TEST_D3D12_WARM
  memset(&profiled,0,sizeof(profiled));profiled.queue=queue;profiled.allocator=allocator;profiled.list=list;
  profiled.destination=dest;profiled.gpu_buffer=gpu_buffer;profiled.readback=readback;profiled.guard=guard;
  profiled.srv_heap=srv_heap;profiled.vertex=vertex;profiled.rtv=rtv[1];profiled.source_srv=srv_gpu[0];profiled.dest_srv=srv_gpu[1];
  profiled.bank_=&b;profiled.ready=prepared_owner;profiled.pipeline=prepared_pipeline;profiled.key=&key;profiled.packet=&packet;
  profiled.pair=(unsigned)pair_index;profiled.msaa=dm;profiled.scale=c;profiled.width=dw;profiled.height=dh;profiled.bytes=active_bytes;
  if(!warm_profile(&profiled)){status=E_FAIL;goto done;}
#endif
  ID3D12Resource_Release(source);ID3D12Resource_Release(dest);source=dest=NULL;++cases;
 }
#ifdef XE_TEST_D3D12_WARM
 if(cases!=90u){status=E_FAIL;goto done;}
 fprintf(stderr,"PASS: 90 warmed D3D12 cases, both image implementations, exact GPU work counts; 2880 raw samples\n");
#elif defined(XE_TEST_PRODUCTION_UINT_BANK)
 if(cases!=(native2?3888u:1944u)){status=E_FAIL;goto done;}
#else
 if(cases!=(native2?7776u:3888u)){status=E_FAIL;goto done;}
#endif
#if defined(XE_TEST_PRODUCTION_UINT_BANK) && !defined(XE_TEST_D3D12_WARM)
 printf("PASS: %s %u production-bank MSAA image transfers, all36 pairs/six scales/builtin, native2=%d, inactive samples and probe tails\n",hardware?"hardware D3D12":"WARP",cases,native2);
#elif !defined(XE_TEST_D3D12_WARM)
 printf("PASS: %s %u MSAA image transfers, all36 pairs/six scales/builtin+manual, native2=%d, inactive samples and probe tails\n",hardware?"hardware D3D12":"WARP",cases,native2);
#endif
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
 if(source)ID3D12Resource_Release(source);
 if(dest)ID3D12Resource_Release(dest);
 if(gpu_buffer)ID3D12Resource_Release(gpu_buffer);
 if(readback)ID3D12Resource_Release(readback);
 if(guard)ID3D12Resource_Release(guard);
 if(vertices)ID3D12Resource_Release(vertices);
 if(srv_heap)ID3D12DescriptorHeap_Release(srv_heap);
 if(rtv_heap)ID3D12DescriptorHeap_Release(rtv_heap);
 if(device)ID3D12Device_Release(device);
 if(adapter)IDXGIAdapter_Release(adapter);
 if(factory)IDXGIFactory4_Release(factory);
 return FAILED(status)?1:0;
}
