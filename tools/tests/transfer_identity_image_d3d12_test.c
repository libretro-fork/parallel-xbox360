/* Numeric identity raster through WARP, exhaustive UNORM16 codes. BSD; C89. */
#define XE_TRANSFER_IMAGE_NUMERIC
#include "transfer_uint_d3d12_image.h"
#include "transfer_identity_image_cases.h"
#include "libretro/core_transfer_identity_d3d12.h"
#include "transfer_test_adapter.h"
int main(int argc,char **argv) {
 static const unsigned formats[]={0,1,2,3};
 static const unsigned scales[4][4]={{2,3,2,3},{2,3,1,1},{1,1,2,3},{1,1,1,1}};
 enum {BYTES=4194816,VERTEX_OFFSET=4194304};
 IDXGIFactory4 *factory=NULL;IDXGIAdapter *adapter=NULL;ID3D12Device *device=NULL;
 ID3D12RootSignature *root=NULL;ID3D12PipelineState *pipeline[4][4]={{0}};
 ID3D12RootSignature *legacy_root=NULL;ID3D12PipelineState *legacy_pipeline[4][4]={{0}};
 ID3DBlob *legacy_serialized=NULL,*legacy_errors=NULL;void *legacy_ps=NULL;size_t legacy_bytes=0;
 const char *legacy_prefix=getenv("IDENTITY_LEGACY_PREFIX");uint32_t legacy_packet;
 ID3D12CommandQueue *queue=NULL;ID3D12CommandAllocator *allocator=NULL;ID3D12GraphicsCommandList *list=NULL;ID3D12CommandList *submitted;
 ID3D12Resource *upload=NULL,*guard=NULL,*readback=NULL,*source=NULL,*dest=NULL;
 ID3D12DescriptorHeap *srv_heap=NULL,*rtv_heap=NULL;ID3D12Fence *fence=NULL;
 ID3DBlob *serialized=NULL,*errors=NULL;HANDLE event=NULL;
 D3D12_ROOT_SIGNATURE_DESC root_desc;D3D12_ROOT_PARAMETER parameters[2];D3D12_DESCRIPTOR_RANGE srv_range;
 D3D12_GRAPHICS_PIPELINE_STATE_DESC desc;D3D12_INPUT_ELEMENT_DESC input;
 D3D12_DESCRIPTOR_HEAP_DESC heap_desc;D3D12_SHADER_RESOURCE_VIEW_DESC srv;
 D3D12_CPU_DESCRIPTOR_HANDLE cpu,rtv;D3D12_GPU_DESCRIPTOR_HANDLE gpu;
 D3D12_COMMAND_QUEUE_DESC queue_desc;D3D12_VERTEX_BUFFER_VIEW vertex;
 D3D12_TEXTURE_COPY_LOCATION image_location,buffer_location;D3D12_VIEWPORT viewport;D3D12_RECT scissor;D3D12_RANGE range;
 xe_transfer_identity_options options={0};xe_transfer_address_key key={0};xe_transfer_identity_packet packet;
 xe_transfer_identity_d3d12_bank bank={0};xe_transfer_identity_d3d12_config bank_config={0};
 xe_transfer_identity_d3d12_ops bank_ops={0};ID3D12PipelineState *active_pipeline;ID3D12RootSignature *active_root;
 void *ps[4]={0},*vs=NULL,*upload_data=NULL,*guard_data=NULL,*readback_data=NULL;
 size_t ps_bytes[4]={0},vs_bytes=0;char path[4096];HRESULT status=E_FAIL;
 unsigned s,c,x,y,lane,sw,sh,dw,dh,components,bytes,source_pitch,dest_pitch,cases=0,variant;
 uint32_t wanted[4],actual;int pending=0,hardware=getenv("MSAA_HARDWARE")!=NULL;
 DXGI_ADAPTER_DESC adapter_desc;
 if(argc!=2 || strlen(argv[1])>2048 || (legacy_prefix && strlen(legacy_prefix)>2048))return 1;
 sprintf(path,"%s-vertex.dxil",argv[1]);vs=read_binary(path,&vs_bytes);
 if(!vs)goto done;
 for(c=0;c<4;++c){sprintf(path,"%s-n1-d0-s0-c%u-t0-m0.dxil",argv[1],c);ps[c]=read_binary(path,&ps_bytes[c]);if(!ps[c])goto done;}
#define CHECK(expression) do {status=(expression);if(FAILED(status)){fprintf(stderr,"Integer WARP line%u status%08lx\n",(unsigned)__LINE__,(unsigned long)status);goto done;}}while(0)
 CHECK(CreateDXGIFactory1(&IID_IDXGIFactory4,(void**)&factory));CHECK(transfer_test_adapter(factory,hardware,&adapter));
 CHECK(IDXGIAdapter_GetDesc(adapter,&adapter_desc));
 fprintf(stderr,"IDENTITY_D3D12_ADAPTER hardware%d vendor%u device%u revision%u\n",hardware,adapter_desc.VendorId,adapter_desc.DeviceId,adapter_desc.Revision);
 CHECK(D3D12CreateDevice((IUnknown*)adapter,D3D_FEATURE_LEVEL_11_0,&IID_ID3D12Device,(void**)&device));
 memset(&root_desc,0,sizeof(root_desc));memset(parameters,0,sizeof(parameters));memset(&srv_range,0,sizeof(srv_range));
 srv_range.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;srv_range.NumDescriptors=1;
 parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;parameters[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;parameters[0].DescriptorTable.NumDescriptorRanges=1;parameters[0].DescriptorTable.pDescriptorRanges=&srv_range;
 parameters[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;parameters[1].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;parameters[1].Constants.ShaderRegister=1;parameters[1].Constants.RegisterSpace=31;parameters[1].Constants.Num32BitValues=5;
 root_desc.NumParameters=2;root_desc.pParameters=parameters;root_desc.Flags=D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
 CHECK(D3D12SerializeRootSignature(&root_desc,D3D_ROOT_SIGNATURE_VERSION_1,&serialized,&errors));CHECK(ID3D12Device_CreateRootSignature(device,0,ID3D10Blob_GetBufferPointer(serialized),ID3D10Blob_GetBufferSize(serialized),&IID_ID3D12RootSignature,(void**)&root));
 memset(&desc,0,sizeof(desc));memset(&input,0,sizeof(input));desc.pRootSignature=root;desc.VS.pShaderBytecode=vs;desc.VS.BytecodeLength=vs_bytes;desc.PS.pShaderBytecode=ps[0];desc.PS.BytecodeLength=ps_bytes[0];
 input.SemanticName="POSITION";input.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;input.InputSlotClass=D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA;input.InstanceDataStepRate=1;
 desc.InputLayout.NumElements=1;desc.InputLayout.pInputElementDescs=&input;desc.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
 desc.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;desc.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;desc.RasterizerState.DepthClipEnable=TRUE;
 desc.BlendState.RenderTarget[0].RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;desc.SampleMask=UINT_MAX;desc.SampleDesc.Count=1;desc.NumRenderTargets=1;
 for(s=0;s<4;++s){desc.RTVFormats[0]=format(s);for(c=0;c<4;++c){desc.PS.pShaderBytecode=ps[c];desc.PS.BytecodeLength=ps_bytes[c];CHECK(ID3D12Device_CreateGraphicsPipelineState(device,&desc,&IID_ID3D12PipelineState,(void**)&pipeline[s][c]));}}
 if(legacy_prefix) {
  parameters[1].Constants.Num32BitValues=4;
  CHECK(D3D12SerializeRootSignature(&root_desc,D3D_ROOT_SIGNATURE_VERSION_1,&legacy_serialized,&legacy_errors));
  CHECK(ID3D12Device_CreateRootSignature(device,0,ID3D10Blob_GetBufferPointer(legacy_serialized),ID3D10Blob_GetBufferSize(legacy_serialized),&IID_ID3D12RootSignature,(void**)&legacy_root));
  desc.pRootSignature=legacy_root;
  for(s=0;s<4;++s)for(c=0;c<4;++c) {
   sprintf(path,"%s-ss-v%u-c%u.dxil",legacy_prefix,s,c);legacy_ps=read_binary(path,&legacy_bytes);
   if(!legacy_ps){status=E_FAIL;goto done;}
   desc.RTVFormats[0]=format(s);desc.PS.pShaderBytecode=legacy_ps;desc.PS.BytecodeLength=legacy_bytes;
   CHECK(ID3D12Device_CreateGraphicsPipelineState(device,&desc,&IID_ID3D12PipelineState,(void**)&legacy_pipeline[s][c]));
   free(legacy_ps);legacy_ps=NULL;
  }
 }
 bank_config.device=device;bank_config.vertex=vs;bank_config.vertex_bytes=vs_bytes;
 bank_config.native2=1;bank_config.scale_x=2;bank_config.scale_y=3;
 for(s=0;s<4;++s)bank_config.sample_mask[s]=1;
 if(!xe_transfer_identity_d3d12_native_ops(&bank_ops) ||
  !xe_transfer_identity_d3d12_init(&bank,&bank_config,&bank_ops))goto done;
 CHECK(buffer(device,D3D12_HEAP_TYPE_UPLOAD,BYTES,&upload));CHECK(buffer(device,D3D12_HEAP_TYPE_UPLOAD,BYTES,&guard));CHECK(buffer(device,D3D12_HEAP_TYPE_READBACK,BYTES,&readback));
 range.Begin=range.End=0;CHECK(ID3D12Resource_Map(upload,0,&range,&upload_data));CHECK(ID3D12Resource_Map(guard,0,&range,&guard_data));memset(guard_data,0x6d,BYTES);
 {float *v=(float*)((unsigned char*)upload_data+VERTEX_OFFSET);v[0]=v[1]=-1;v[2]=v[3]=1;}
 memset(&heap_desc,0,sizeof(heap_desc));heap_desc.NumDescriptors=1;heap_desc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;heap_desc.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
 CHECK(ID3D12Device_CreateDescriptorHeap(device,&heap_desc,&IID_ID3D12DescriptorHeap,(void**)&srv_heap));ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(srv_heap,&cpu);ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(srv_heap,&gpu);
 heap_desc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;heap_desc.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_NONE;CHECK(ID3D12Device_CreateDescriptorHeap(device,&heap_desc,&IID_ID3D12DescriptorHeap,(void**)&rtv_heap));ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(rtv_heap,&rtv);
 memset(&queue_desc,0,sizeof(queue_desc));CHECK(ID3D12Device_CreateCommandQueue(device,&queue_desc,&IID_ID3D12CommandQueue,(void**)&queue));CHECK(ID3D12Device_CreateCommandAllocator(device,D3D12_COMMAND_LIST_TYPE_DIRECT,&IID_ID3D12CommandAllocator,(void**)&allocator));
 CHECK(ID3D12Device_CreateCommandList(device,0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator,NULL,&IID_ID3D12GraphicsCommandList,(void**)&list));CHECK(ID3D12GraphicsCommandList_Close(list));
 CHECK(ID3D12Device_CreateFence(device,0,D3D12_FENCE_FLAG_NONE,&IID_ID3D12Fence,(void**)&fence));event=CreateEventA(NULL,FALSE,FALSE,NULL);if(!event){status=E_FAIL;goto done;}
 key.source_color=key.dest_color=1;key.source_pitch=key.dest_pitch=4;key.source_native2=key.dest_native2=1;
 for(variant=0;variant<(legacy_prefix?3u:2u);++variant)for(s=0;s<4;++s)for(c=0;c<4;++c) {
  active_pipeline=variant?xe_transfer_identity_d3d12_get(&bank,s,0,0,c>>1,c&1):pipeline[s][c];
  active_root=variant?bank.root:root;if(!active_pipeline){status=E_FAIL;goto done;}
  if(variant==2){active_pipeline=legacy_pipeline[s][c];active_root=legacy_root;}
  options.view=s;
  key.dest_scale_x=scales[c][0];key.dest_scale_y=scales[c][1];key.source_scale_x=scales[c][2];key.source_scale_y=scales[c][3];
  if(!xe_transfer_identity_packet_prepare(&packet,&options,&key,formats[s],formats[s],s==1,0)){status=E_FAIL;goto done;}
  sw=320*key.source_scale_x;sh=208*key.source_scale_y;dw=320*key.dest_scale_x;dh=208*key.dest_scale_y;
  CHECK(texture(device,formats[s],sw,sh,0,&source));CHECK(texture(device,formats[s],dw,dh,1,&dest));
  components=identity_components(s);bytes=identity_bytes(s);source_pitch=(sw*components*bytes+255)&~255u;
  memset(upload_data,0x6d,VERTEX_OFFSET);
  for(y=0;y<sh;++y)for(x=0;x<sw;++x)for(lane=0;lane<components;++lane){void *row=(unsigned char*)upload_data+y*source_pitch;if(bytes==1)((unsigned char*)row)[x*components+lane]=(unsigned char)identity_pattern(s,x,y,lane);else if(bytes==2)((uint16_t*)row)[x*components+lane]=(uint16_t)identity_pattern(s,x,y,lane);else((uint32_t*)row)[x*components+lane]=identity_pattern(s,x,y,lane);}
  memset(&srv,0,sizeof(srv));srv.Format=format(formats[s]);srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;
  ID3D12Device_CreateShaderResourceView(device,source,&srv,cpu);ID3D12Device_CreateRenderTargetView(device,dest,NULL,rtv);

  CHECK(ID3D12CommandAllocator_Reset(allocator));CHECK(ID3D12GraphicsCommandList_Reset(list,allocator,active_pipeline));
  ID3D12GraphicsCommandList_CopyBufferRegion(list,readback,0,guard,0,BYTES);
  memset(&image_location,0,sizeof(image_location));image_location.pResource=source;image_location.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  memset(&buffer_location,0,sizeof(buffer_location));buffer_location.pResource=upload;buffer_location.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;buffer_location.PlacedFootprint.Footprint.Format=format(formats[s]);buffer_location.PlacedFootprint.Footprint.Width=sw;buffer_location.PlacedFootprint.Footprint.Height=sh;buffer_location.PlacedFootprint.Footprint.Depth=1;buffer_location.PlacedFootprint.Footprint.RowPitch=source_pitch;
  ID3D12GraphicsCommandList_CopyTextureRegion(list,&image_location,0,0,0,&buffer_location,NULL);transition(list,source,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  ID3D12GraphicsCommandList_SetGraphicsRootSignature(list,active_root);ID3D12GraphicsCommandList_SetDescriptorHeaps(list,1,&srv_heap);ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(list,0,gpu);
  if(variant==2) {
   legacy_packet=key.dest_pitch|(key.source_pitch<<10)|(((uint32_t)key.dest_to_source_base&4095)<<20);
   ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(list,1,1,&legacy_packet,0);
  } else ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(list,1,5,&packet,0);
  memset(&viewport,0,sizeof(viewport));viewport.Width=(float)dw;viewport.Height=(float)dh;viewport.MaxDepth=1;scissor.left=scissor.top=0;scissor.right=(LONG)dw;scissor.bottom=(LONG)dh;
  ID3D12GraphicsCommandList_RSSetViewports(list,1,&viewport);ID3D12GraphicsCommandList_RSSetScissorRects(list,1,&scissor);ID3D12GraphicsCommandList_OMSetRenderTargets(list,1,&rtv,FALSE,NULL);
  vertex.BufferLocation=ID3D12Resource_GetGPUVirtualAddress(upload)+VERTEX_OFFSET;vertex.SizeInBytes=vertex.StrideInBytes=16;ID3D12GraphicsCommandList_IASetVertexBuffers(list,0,1,&vertex);ID3D12GraphicsCommandList_IASetPrimitiveTopology(list,D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);ID3D12GraphicsCommandList_DrawInstanced(list,6,1,0,0);
  transition(list,dest,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);
  components=identity_components(s);bytes=identity_bytes(s);dest_pitch=(dw*components*bytes+255)&~255u;
  image_location.pResource=dest;buffer_location.pResource=readback;buffer_location.PlacedFootprint.Footprint.Format=format(formats[s]);buffer_location.PlacedFootprint.Footprint.Width=dw;buffer_location.PlacedFootprint.Footprint.Height=dh;buffer_location.PlacedFootprint.Footprint.RowPitch=dest_pitch;
  ID3D12GraphicsCommandList_CopyTextureRegion(list,&buffer_location,0,0,0,&image_location,NULL);CHECK(ID3D12GraphicsCommandList_Close(list));submitted=(ID3D12CommandList*)list;ID3D12CommandQueue_ExecuteCommandLists(queue,1,&submitted);pending=1;
  CHECK(ID3D12CommandQueue_Signal(queue,fence,cases+1));CHECK(ID3D12Fence_SetEventOnCompletion(fence,cases+1,event));if(WaitForSingleObject(event,10000)!=WAIT_OBJECT_0){status=E_FAIL;goto done;}pending=0;
  range.Begin=0;range.End=BYTES;CHECK(ID3D12Resource_Map(readback,0,&range,&readback_data));
  for(y=0;y<dh;++y)for(x=0;x<dw;++x){identity_expected(&key,s,x,y,wanted);for(lane=0;lane<components;++lane){const void *row=(const unsigned char*)readback_data+y*dest_pitch;actual=bytes==1?((const unsigned char*)row)[x*components+lane]:bytes==2?((const uint16_t*)row)[x*components+lane]:((const uint32_t*)row)[x*components+lane];if(actual!=wanted[lane]){fprintf(stderr,"WARP image sf%u df%u scale%u pixel%u,%u lane%u %08x != %08x\n",formats[s],formats[s],c,x,y,lane,actual,wanted[lane]);status=E_FAIL;goto done;}}}
  for(y=0;y<dh;++y)for(x=dw*components*bytes;x<dest_pitch;++x)if(((const unsigned char*)readback_data)[y*dest_pitch+x]!=0x6d){status=E_FAIL;goto done;}
  for(x=dh*dest_pitch;x<BYTES;++x)if(((const unsigned char*)readback_data)[x]!=0x6d){status=E_FAIL;goto done;}
  range.Begin=range.End=0;ID3D12Resource_Unmap(readback,0,&range);readback_data=NULL;ID3D12Resource_Release(source);ID3D12Resource_Release(dest);source=dest=NULL;++cases;
 }
 printf("PASS: %s %u numeric identity SS raster cases, standalone and actual prepared bank, all65536 UNORM16 codes, RGBA8/RGB10A2/finite-half encodings, four native/asymmetric scales and tail guards\n",hardware?"hardware D3D12":"WARP",cases);status=S_OK;
 if(legacy_prefix)printf("PASS: existing native numeric emitter images match the same independent legacy address oracle\n");
done:
 if(pending){fprintf(stderr,"Pending WARP test resources retained\n");exit(1);}
 if(!xe_transfer_identity_d3d12_close(&bank,&bank_ops))exit(1);
 if(readback_data)ID3D12Resource_Unmap(readback,0,NULL);
 if(upload_data)ID3D12Resource_Unmap(upload,0,NULL);
 if(guard_data)ID3D12Resource_Unmap(guard,0,NULL);
 if(event)CloseHandle(event);
 if(fence)ID3D12Fence_Release(fence);
 if(list)ID3D12GraphicsCommandList_Release(list);
 if(allocator)ID3D12CommandAllocator_Release(allocator);
 if(queue)ID3D12CommandQueue_Release(queue);
 for(s=0;s<4;++s)for(c=0;c<4;++c)if(pipeline[s][c])ID3D12PipelineState_Release(pipeline[s][c]);
 for(s=0;s<4;++s)for(c=0;c<4;++c)if(legacy_pipeline[s][c])ID3D12PipelineState_Release(legacy_pipeline[s][c]);
 if(legacy_root)ID3D12RootSignature_Release(legacy_root);
 if(legacy_serialized)ID3D10Blob_Release(legacy_serialized);
 if(legacy_errors)ID3D10Blob_Release(legacy_errors);
 free(legacy_ps);
 if(srv_heap)ID3D12DescriptorHeap_Release(srv_heap);
 if(rtv_heap)ID3D12DescriptorHeap_Release(rtv_heap);
 if(source)ID3D12Resource_Release(source);
 if(dest)ID3D12Resource_Release(dest);
 if(readback)ID3D12Resource_Release(readback);
 if(guard)ID3D12Resource_Release(guard);
 if(upload)ID3D12Resource_Release(upload);
 if(root)ID3D12RootSignature_Release(root);
 if(errors)ID3D10Blob_Release(errors);
 if(serialized)ID3D10Blob_Release(serialized);
 if(device)ID3D12Device_Release(device);
 if(adapter)IDXGIAdapter_Release(adapter);
 if(factory)IDXGIFactory4_Release(factory);
 for(c=0;c<4;++c)free(ps[c]);free(vs);return FAILED(status)?1:0;
}
