/* Actual raw unsigned texture/fragment transfers through WARP. BSD; C89. */
#include "transfer_uint_image_cases.h"
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <initguid.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void *read_binary(const char *path,size_t *bytes) {
 FILE *f=fopen(path,"rb");long n;void *p;
 if(!f)return NULL;
 if(fseek(f,0,SEEK_END) || (n=ftell(f))<=0 || n>1048576 || fseek(f,0,SEEK_SET)){fclose(f);return NULL;}
 p=malloc((size_t)n);if(!p || fread(p,1,(size_t)n,f)!=(size_t)n){free(p);fclose(f);return NULL;}
 fclose(f);*bytes=(size_t)n;return p;
}
static DXGI_FORMAT format(unsigned f) {
 return f==5 || f==7?DXGI_FORMAT_R16G16B16A16_UINT:f==14?DXGI_FORMAT_R32_UINT:f==15?DXGI_FORMAT_R32G32_UINT:DXGI_FORMAT_R16G16_UINT;
}
static HRESULT buffer(ID3D12Device *device,D3D12_HEAP_TYPE type,UINT64 bytes,ID3D12Resource **out) {
 D3D12_HEAP_PROPERTIES heap;D3D12_RESOURCE_DESC desc;
 memset(&heap,0,sizeof(heap));heap.Type=type;memset(&desc,0,sizeof(desc));
 desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=bytes;desc.Height=desc.DepthOrArraySize=desc.MipLevels=1;
 desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
 return ID3D12Device_CreateCommittedResource(device,&heap,D3D12_HEAP_FLAG_NONE,&desc,
  type==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_DEST,NULL,&IID_ID3D12Resource,(void**)out);
}
static HRESULT texture(ID3D12Device *device,unsigned f,unsigned w,unsigned h,int target,ID3D12Resource **out) {
 D3D12_HEAP_PROPERTIES heap;D3D12_RESOURCE_DESC desc;
 memset(&heap,0,sizeof(heap));heap.Type=D3D12_HEAP_TYPE_DEFAULT;memset(&desc,0,sizeof(desc));
 desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=w;desc.Height=h;desc.DepthOrArraySize=desc.MipLevels=1;
 desc.SampleDesc.Count=1;desc.Format=format(f);desc.Flags=target?D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET:D3D12_RESOURCE_FLAG_NONE;
 return ID3D12Device_CreateCommittedResource(device,&heap,D3D12_HEAP_FLAG_NONE,&desc,
  target?D3D12_RESOURCE_STATE_RENDER_TARGET:D3D12_RESOURCE_STATE_COPY_DEST,NULL,&IID_ID3D12Resource,(void**)out);
}
static void transition(ID3D12GraphicsCommandList *list,ID3D12Resource *resource,D3D12_RESOURCE_STATES old,D3D12_RESOURCE_STATES next) {
 D3D12_RESOURCE_BARRIER b;memset(&b,0,sizeof(b));b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
 b.Transition.pResource=resource;b.Transition.StateBefore=old;b.Transition.StateAfter=next;b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
 ID3D12GraphicsCommandList_ResourceBarrier(list,1,&b);
}
int main(int argc,char **argv) {
 static const unsigned formats[]={4,5,6,7,14,15};
 static const unsigned scales[6][4]={{1,1,1,1},{2,2,2,2},{3,5,3,5},{1,1,3,5},{3,5,1,1},{1,1,1,3}};
 enum {BYTES=262144,VERTEX_OFFSET=131072};
 IDXGIFactory4 *factory=NULL;IDXGIAdapter *adapter=NULL;ID3D12Device *device=NULL;
 ID3D12RootSignature *root=NULL;ID3D12PipelineState *pipeline[4]={0};
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
 xe_transfer_uint_options options={0};xe_transfer_address_key key={0};xe_transfer_uint_packet packet;
 void *ps=NULL,*vs=NULL,*upload_data=NULL,*guard_data=NULL,*readback_data=NULL;
 size_t ps_bytes=0,vs_bytes=0;char path[4096];HRESULT status=E_FAIL;
 unsigned s,d,c,x,y,lane,index,sw,sh,dw,dh,components,bytes,source_pitch,dest_pitch,cases=0;
 uint32_t wanted[4],actual;int pending=0;
 if(argc!=2 || strlen(argv[1])>2048)return 1;
 sprintf(path,"%s-vertex.dxil",argv[1]);vs=read_binary(path,&vs_bytes);
 sprintf(path,"%s-s0-m0-t0.dxil",argv[1]);ps=read_binary(path,&ps_bytes);if(!ps || !vs)goto done;
#define CHECK(expression) do {status=(expression);if(FAILED(status)){fprintf(stderr,"Integer WARP line%u status%08lx\n",(unsigned)__LINE__,(unsigned long)status);goto done;}}while(0)
 CHECK(CreateDXGIFactory1(&IID_IDXGIFactory4,(void**)&factory));CHECK(IDXGIFactory4_EnumWarpAdapter(factory,&IID_IDXGIAdapter,(void**)&adapter));
 CHECK(D3D12CreateDevice((IUnknown*)adapter,D3D_FEATURE_LEVEL_11_0,&IID_ID3D12Device,(void**)&device));
 memset(&root_desc,0,sizeof(root_desc));memset(parameters,0,sizeof(parameters));memset(&srv_range,0,sizeof(srv_range));
 srv_range.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;srv_range.NumDescriptors=1;
 parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;parameters[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;parameters[0].DescriptorTable.NumDescriptorRanges=1;parameters[0].DescriptorTable.pDescriptorRanges=&srv_range;
 parameters[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;parameters[1].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;parameters[1].Constants.ShaderRegister=1;parameters[1].Constants.RegisterSpace=31;parameters[1].Constants.Num32BitValues=28;
 root_desc.NumParameters=2;root_desc.pParameters=parameters;root_desc.Flags=D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
 CHECK(D3D12SerializeRootSignature(&root_desc,D3D_ROOT_SIGNATURE_VERSION_1,&serialized,&errors));CHECK(ID3D12Device_CreateRootSignature(device,0,ID3D10Blob_GetBufferPointer(serialized),ID3D10Blob_GetBufferSize(serialized),&IID_ID3D12RootSignature,(void**)&root));
 memset(&desc,0,sizeof(desc));memset(&input,0,sizeof(input));desc.pRootSignature=root;desc.VS.pShaderBytecode=vs;desc.VS.BytecodeLength=vs_bytes;desc.PS.pShaderBytecode=ps;desc.PS.BytecodeLength=ps_bytes;
 input.SemanticName="POSITION";input.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;input.InputSlotClass=D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA;input.InstanceDataStepRate=1;
 desc.InputLayout.NumElements=1;desc.InputLayout.pInputElementDescs=&input;desc.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
 desc.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;desc.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;desc.RasterizerState.DepthClipEnable=TRUE;
 desc.BlendState.RenderTarget[0].RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;desc.SampleMask=UINT_MAX;desc.SampleDesc.Count=1;desc.NumRenderTargets=1;
 for(c=0;c<4;++c){desc.RTVFormats[0]=format(c==0?4:c==1?5:c==2?14:15);CHECK(ID3D12Device_CreateGraphicsPipelineState(device,&desc,&IID_ID3D12PipelineState,(void**)&pipeline[c]));}
 CHECK(buffer(device,D3D12_HEAP_TYPE_UPLOAD,BYTES,&upload));CHECK(buffer(device,D3D12_HEAP_TYPE_UPLOAD,BYTES,&guard));CHECK(buffer(device,D3D12_HEAP_TYPE_READBACK,BYTES,&readback));
 range.Begin=range.End=0;CHECK(ID3D12Resource_Map(upload,0,&range,&upload_data));CHECK(ID3D12Resource_Map(guard,0,&range,&guard_data));memset(guard_data,0x6d,BYTES);
 {float *v=(float*)((unsigned char*)upload_data+VERTEX_OFFSET);v[0]=v[1]=-1;v[2]=v[3]=1;}
 memset(&heap_desc,0,sizeof(heap_desc));heap_desc.NumDescriptors=1;heap_desc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;heap_desc.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
 CHECK(ID3D12Device_CreateDescriptorHeap(device,&heap_desc,&IID_ID3D12DescriptorHeap,(void**)&srv_heap));ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(srv_heap,&cpu);ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(srv_heap,&gpu);
 heap_desc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;heap_desc.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_NONE;CHECK(ID3D12Device_CreateDescriptorHeap(device,&heap_desc,&IID_ID3D12DescriptorHeap,(void**)&rtv_heap));ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(rtv_heap,&rtv);
 memset(&queue_desc,0,sizeof(queue_desc));CHECK(ID3D12Device_CreateCommandQueue(device,&queue_desc,&IID_ID3D12CommandQueue,(void**)&queue));CHECK(ID3D12Device_CreateCommandAllocator(device,D3D12_COMMAND_LIST_TYPE_DIRECT,&IID_ID3D12CommandAllocator,(void**)&allocator));
 CHECK(ID3D12Device_CreateCommandList(device,0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator,NULL,&IID_ID3D12GraphicsCommandList,(void**)&list));CHECK(ID3D12GraphicsCommandList_Close(list));
 CHECK(ID3D12Device_CreateFence(device,0,D3D12_FENCE_FLAG_NONE,&IID_ID3D12Fence,(void**)&fence));event=CreateEventA(NULL,FALSE,FALSE,NULL);if(!event){status=E_FAIL;goto done;}
 key.source_color=key.dest_color=1;key.source_pitch=key.dest_pitch=1;
 for(s=0;s<6;++s)for(d=0;d<6;++d)for(c=0;c<6;++c) {
  key.source_wide=formats[s]==5 || formats[s]==7 || formats[s]==15;key.dest_wide=formats[d]==5 || formats[d]==7 || formats[d]==15;
  key.dest_scale_x=scales[c][0];key.dest_scale_y=scales[c][1];key.source_scale_x=scales[c][2];key.source_scale_y=scales[c][3];
  if(!xe_transfer_uint_packet_prepare(&packet,&options,&key,formats[s],formats[d],0)){status=E_FAIL;goto done;}
  sw=packet.address.word[XE_ADDR_S_TW];sh=packet.address.word[XE_ADDR_S_TH];dw=packet.address.word[XE_ADDR_D_TW];dh=packet.address.word[XE_ADDR_D_TH];
  CHECK(texture(device,formats[s],sw,sh,0,&source));CHECK(texture(device,formats[d],dw,dh,1,&dest));
  components=image_components(formats[s]);bytes=image_halves(formats[s])?2:4;source_pitch=(sw*components*bytes+255)&~255u;
  memset(upload_data,0x6d,VERTEX_OFFSET);
  for(y=0;y<sh;++y)for(x=0;x<sw;++x)for(lane=0;lane<components;++lane){void *row=(unsigned char*)upload_data+y*source_pitch;if(bytes==2)((uint16_t*)row)[x*components+lane]=(uint16_t)image_pattern(formats[s],x,y,lane);else((uint32_t*)row)[x*components+lane]=image_pattern(formats[s],x,y,lane);}
  memset(&srv,0,sizeof(srv));srv.Format=format(formats[s]);srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;
  ID3D12Device_CreateShaderResourceView(device,source,&srv,cpu);ID3D12Device_CreateRenderTargetView(device,dest,NULL,rtv);
  index=formats[d]==5 || formats[d]==7?1:formats[d]==14?2:formats[d]==15?3:0;
  CHECK(ID3D12CommandAllocator_Reset(allocator));CHECK(ID3D12GraphicsCommandList_Reset(list,allocator,pipeline[index]));
  ID3D12GraphicsCommandList_CopyBufferRegion(list,readback,0,guard,0,BYTES);
  memset(&image_location,0,sizeof(image_location));image_location.pResource=source;image_location.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  memset(&buffer_location,0,sizeof(buffer_location));buffer_location.pResource=upload;buffer_location.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;buffer_location.PlacedFootprint.Footprint.Format=format(formats[s]);buffer_location.PlacedFootprint.Footprint.Width=sw;buffer_location.PlacedFootprint.Footprint.Height=sh;buffer_location.PlacedFootprint.Footprint.Depth=1;buffer_location.PlacedFootprint.Footprint.RowPitch=source_pitch;
  ID3D12GraphicsCommandList_CopyTextureRegion(list,&image_location,0,0,0,&buffer_location,NULL);transition(list,source,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  ID3D12GraphicsCommandList_SetGraphicsRootSignature(list,root);ID3D12GraphicsCommandList_SetDescriptorHeaps(list,1,&srv_heap);ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(list,0,gpu);ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(list,1,28,&packet,0);
  memset(&viewport,0,sizeof(viewport));viewport.Width=(float)dw;viewport.Height=(float)dh;viewport.MaxDepth=1;scissor.left=scissor.top=0;scissor.right=(LONG)dw;scissor.bottom=(LONG)dh;
  ID3D12GraphicsCommandList_RSSetViewports(list,1,&viewport);ID3D12GraphicsCommandList_RSSetScissorRects(list,1,&scissor);ID3D12GraphicsCommandList_OMSetRenderTargets(list,1,&rtv,FALSE,NULL);
  vertex.BufferLocation=ID3D12Resource_GetGPUVirtualAddress(upload)+VERTEX_OFFSET;vertex.SizeInBytes=vertex.StrideInBytes=16;ID3D12GraphicsCommandList_IASetVertexBuffers(list,0,1,&vertex);ID3D12GraphicsCommandList_IASetPrimitiveTopology(list,D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);ID3D12GraphicsCommandList_DrawInstanced(list,6,1,0,0);
  transition(list,dest,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);
  components=image_components(formats[d]);bytes=image_halves(formats[d])?2:4;dest_pitch=(dw*components*bytes+255)&~255u;
  image_location.pResource=dest;buffer_location.pResource=readback;buffer_location.PlacedFootprint.Footprint.Format=format(formats[d]);buffer_location.PlacedFootprint.Footprint.Width=dw;buffer_location.PlacedFootprint.Footprint.Height=dh;buffer_location.PlacedFootprint.Footprint.RowPitch=dest_pitch;
  ID3D12GraphicsCommandList_CopyTextureRegion(list,&buffer_location,0,0,0,&image_location,NULL);CHECK(ID3D12GraphicsCommandList_Close(list));submitted=(ID3D12CommandList*)list;ID3D12CommandQueue_ExecuteCommandLists(queue,1,&submitted);pending=1;
  CHECK(ID3D12CommandQueue_Signal(queue,fence,cases+1));CHECK(ID3D12Fence_SetEventOnCompletion(fence,cases+1,event));if(WaitForSingleObject(event,10000)!=WAIT_OBJECT_0){status=E_FAIL;goto done;}pending=0;
  range.Begin=0;range.End=BYTES;CHECK(ID3D12Resource_Map(readback,0,&range,&readback_data));
  for(y=0;y<dh;++y)for(x=0;x<dw;++x){image_expected(&key,formats[s],formats[d],x,y,wanted);for(lane=0;lane<components;++lane){const void *row=(const unsigned char*)readback_data+y*dest_pitch;actual=bytes==2?((const uint16_t*)row)[x*components+lane]:((const uint32_t*)row)[x*components+lane];if(actual!=wanted[lane]){fprintf(stderr,"WARP image sf%u df%u scale%u pixel%u,%u lane%u %08x != %08x\n",formats[s],formats[d],c,x,y,lane,actual,wanted[lane]);status=E_FAIL;goto done;}}}
  for(y=0;y<dh;++y)for(x=dw*components*bytes;x<dest_pitch;++x)if(((const unsigned char*)readback_data)[y*dest_pitch+x]!=0x6d){status=E_FAIL;goto done;}
  for(x=dh*dest_pitch;x<BYTES;++x)if(((const unsigned char*)readback_data)[x]!=0x6d){status=E_FAIL;goto done;}
  range.Begin=range.End=0;ID3D12Resource_Unmap(readback,0,&range);readback_data=NULL;ID3D12Resource_Release(source);ID3D12Resource_Release(dest);source=dest=NULL;++cases;
 }
 printf("PASS: WARP %u actual integer SS raster transfers, all36 pairs/six scales, two modules/four precreated PSOs, row/tail guards\n",cases);status=S_OK;
done:
 if(pending){fprintf(stderr,"Pending WARP test resources retained\n");exit(1);}
 if(readback_data)ID3D12Resource_Unmap(readback,0,NULL);
 if(upload_data)ID3D12Resource_Unmap(upload,0,NULL);
 if(guard_data)ID3D12Resource_Unmap(guard,0,NULL);
 if(event)CloseHandle(event);
 if(fence)ID3D12Fence_Release(fence);
 if(list)ID3D12GraphicsCommandList_Release(list);
 if(allocator)ID3D12CommandAllocator_Release(allocator);
 if(queue)ID3D12CommandQueue_Release(queue);
 for(c=0;c<4;++c)if(pipeline[c])ID3D12PipelineState_Release(pipeline[c]);
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
 free(ps);free(vs);return FAILED(status)?1:0;
}
