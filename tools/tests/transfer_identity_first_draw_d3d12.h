/* First real transfer after complete bank preparation. Test-only; BSD; C89.
 * Fixture resources/upload/clear are separate from command-record and GPU time.
 * Source/target are normal typed RGBA8. Readback and all guards prove execution. */
#include "gpu_timestamp_measure.h"
typedef struct identity_first_draw {
 double record_seconds,submit_complete_seconds,validation_seconds,gpu_seconds;
 uint64_t primitives,vertices,fragments;unsigned pixels;
} identity_first_draw;
static uint32_t first_pattern(unsigned x,unsigned y) {
 return ((x*13+y*7)&255u)|(((x^(y*11))&255u)<<8)|(((x*3+y*5)&255u)<<16)|UINT32_C(0xa5000000);
}
static int first_wait(ID3D12CommandQueue *queue,ID3D12Fence *fence,HANDLE event,UINT64 serial) {
 UINT64 completed;
 if(FAILED(ID3D12CommandQueue_Signal(queue,fence,serial)) || FAILED(ID3D12Fence_SetEventOnCompletion(fence,serial,event)) || WaitForSingleObject(event,10000)!=WAIT_OBJECT_0) {
  fprintf(stderr,"Pending first-transfer resources retained\n");exit(1);
 }
 completed=ID3D12Fence_GetCompletedValue(fence);return completed!=UINT64_MAX && completed>=serial;
}
static int identity_first_transfer(ID3D12Device *device,const xe_transfer_identity_d3d12_bank *ready,
 const xe_transfer_identity_d3d12_config *config,identity_first_draw *measurement) {
 ID3D12CommandQueue *queue=NULL;ID3D12CommandAllocator *allocator=NULL;ID3D12GraphicsCommandList *list=NULL;ID3D12CommandList *submitted;
 ID3D12Resource *source=NULL,*dest=NULL,*upload=NULL,*guard=NULL,*readback=NULL,*query_data=NULL;
 ID3D12DescriptorHeap *srv_heap=NULL,*rtv_heap=NULL;ID3D12Fence *fence=NULL;HANDLE event=NULL;
 ID3D12QueryHeap *timestamps=NULL,*stats_heap=NULL;D3D12_QUERY_HEAP_DESC query={0};
 D3D12_COMMAND_QUEUE_DESC queue_info={0};D3D12_DESCRIPTOR_HEAP_DESC heap={0};
 D3D12_SHADER_RESOURCE_VIEW_DESC srv={0};D3D12_CPU_DESCRIPTOR_HANDLE cpu,rtv;D3D12_GPU_DESCRIPTOR_HANDLE gpu;
 D3D12_RANGE range={0};D3D12_TEXTURE_COPY_LOCATION image_copy={0},buffer_copy={0};
 D3D12_VERTEX_BUFFER_VIEW vertex;D3D12_VIEWPORT viewport={0};D3D12_RECT scissor={0};
 xe_transfer_identity_options options={0};xe_transfer_address_key key={0};xe_transfer_identity_packet packet;
 ID3D12PipelineState *pipeline;D3D12_QUERY_DATA_PIPELINE_STATISTICS stats;UINT64 raw[2],frequency;
 unsigned w=80*config->scale_x,h=16*config->scale_y,pitch,bytes,x,y,i;void *mapped_upload=NULL,*mapped_guard=NULL,*mapped_readback=NULL,*mapped_query=NULL;
 double before;float period,clear[4]={0,0,0,0};uint64_t ticks[4];int result=0,pending=0;
 if(w>UINT32_MAX/4-255 || h>UINT32_MAX/((w*4+255)&~255u))return 0;
 pitch=(w*4+255)&~255u;bytes=pitch*h+256;
#define FIRST_OK(expression) do {if(FAILED(expression)){fprintf(stderr,"First transfer API line%u\n",(unsigned)__LINE__);goto done;}}while(0)
 FIRST_OK(numeric_samples(device,0,w,h,1,&source));FIRST_OK(numeric_samples(device,0,w,h,1,&dest));
 FIRST_OK(buffer(device,D3D12_HEAP_TYPE_UPLOAD,bytes+16,&upload));FIRST_OK(buffer(device,D3D12_HEAP_TYPE_UPLOAD,bytes,&guard));
 FIRST_OK(buffer(device,D3D12_HEAP_TYPE_READBACK,bytes,&readback));FIRST_OK(buffer(device,D3D12_HEAP_TYPE_READBACK,16+sizeof(stats),&query_data));
 FIRST_OK(ID3D12Resource_Map(upload,0,&range,&mapped_upload));FIRST_OK(ID3D12Resource_Map(guard,0,&range,&mapped_guard));
 memset(mapped_upload,0x6d,bytes+16);memset(mapped_guard,0x6d,bytes);
 for(y=0;y<h;++y)for(x=0;x<w;++x)((uint32_t*)((unsigned char*)mapped_upload+y*pitch))[x]=first_pattern(x,y);
 {float *v=(float*)((unsigned char*)mapped_upload+bytes);v[0]=v[1]=-1;v[2]=v[3]=1;}
 heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;heap.NumDescriptors=1;heap.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
 FIRST_OK(ID3D12Device_CreateDescriptorHeap(device,&heap,&IID_ID3D12DescriptorHeap,(void**)&srv_heap));
 ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(srv_heap,&cpu);ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(srv_heap,&gpu);
 heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;heap.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
 FIRST_OK(ID3D12Device_CreateDescriptorHeap(device,&heap,&IID_ID3D12DescriptorHeap,(void**)&rtv_heap));ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(rtv_heap,&rtv);
 srv.Format=DXGI_FORMAT_R8G8B8A8_UNORM;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;
 ID3D12Device_CreateShaderResourceView(device,source,&srv,cpu);ID3D12Device_CreateRenderTargetView(device,dest,NULL,rtv);
 FIRST_OK(ID3D12Device_CreateCommandQueue(device,&queue_info,&IID_ID3D12CommandQueue,(void**)&queue));
 FIRST_OK(ID3D12Device_CreateCommandAllocator(device,D3D12_COMMAND_LIST_TYPE_DIRECT,&IID_ID3D12CommandAllocator,(void**)&allocator));
 FIRST_OK(ID3D12Device_CreateCommandList(device,0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator,NULL,&IID_ID3D12GraphicsCommandList,(void**)&list));
 FIRST_OK(ID3D12Device_CreateFence(device,0,D3D12_FENCE_FLAG_NONE,&IID_ID3D12Fence,(void**)&fence));event=CreateEventA(NULL,FALSE,FALSE,NULL);if(!event)goto done;
 query.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;query.Count=2;FIRST_OK(ID3D12Device_CreateQueryHeap(device,&query,&IID_ID3D12QueryHeap,(void**)&timestamps));
 query.Type=D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS;query.Count=1;FIRST_OK(ID3D12Device_CreateQueryHeap(device,&query,&IID_ID3D12QueryHeap,(void**)&stats_heap));
 FIRST_OK(ID3D12CommandQueue_GetTimestampFrequency(queue,&frequency));if(!frequency)goto done;period=(float)(1e9/(double)frequency);
 image_copy.pResource=source;image_copy.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
 buffer_copy.pResource=upload;buffer_copy.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;buffer_copy.PlacedFootprint.Footprint.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
 buffer_copy.PlacedFootprint.Footprint.Width=w;buffer_copy.PlacedFootprint.Footprint.Height=h;buffer_copy.PlacedFootprint.Footprint.Depth=1;buffer_copy.PlacedFootprint.Footprint.RowPitch=pitch;
 ID3D12GraphicsCommandList_CopyTextureRegion(list,&image_copy,0,0,0,&buffer_copy,NULL);
 transition(list,source,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
 transition(list,dest,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_RENDER_TARGET);
 ID3D12GraphicsCommandList_ClearRenderTargetView(list,rtv,clear,0,NULL);
 ID3D12GraphicsCommandList_CopyBufferRegion(list,readback,0,guard,0,bytes);
 FIRST_OK(ID3D12GraphicsCommandList_Close(list));submitted=(ID3D12CommandList*)list;ID3D12CommandQueue_ExecuteCommandLists(queue,1,&submitted);pending=1;
 if(!first_wait(queue,fence,event,1))goto done;pending=0;
 key.source_color=key.dest_color=1;key.source_pitch=key.dest_pitch=1;key.source_native2=key.dest_native2=config->native2;
 key.source_scale_x=key.dest_scale_x=config->scale_x;key.source_scale_y=key.dest_scale_y=config->scale_y;
 if(!xe_transfer_identity_packet_prepare(&packet,&options,&key,0,0,0,0))goto done;
 pipeline=xe_transfer_identity_d3d12_get(ready,0,0,0,0,0);if(!pipeline)goto done;
 vertex.BufferLocation=ID3D12Resource_GetGPUVirtualAddress(upload)+bytes;vertex.SizeInBytes=vertex.StrideInBytes=16;
 viewport.Width=(float)w;viewport.Height=(float)h;viewport.MaxDepth=1;scissor.right=(LONG)w;scissor.bottom=(LONG)h;
 before=init_now();FIRST_OK(ID3D12CommandAllocator_Reset(allocator));FIRST_OK(ID3D12GraphicsCommandList_Reset(list,allocator,pipeline));
 ID3D12GraphicsCommandList_SetGraphicsRootSignature(list,ready->root);ID3D12GraphicsCommandList_SetDescriptorHeaps(list,1,&srv_heap);
 ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(list,0,gpu);ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(list,1,5,&packet,0);
 ID3D12GraphicsCommandList_RSSetViewports(list,1,&viewport);ID3D12GraphicsCommandList_RSSetScissorRects(list,1,&scissor);ID3D12GraphicsCommandList_OMSetRenderTargets(list,1,&rtv,FALSE,NULL);
 ID3D12GraphicsCommandList_IASetVertexBuffers(list,0,1,&vertex);ID3D12GraphicsCommandList_IASetPrimitiveTopology(list,D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
 ID3D12GraphicsCommandList_EndQuery(list,timestamps,D3D12_QUERY_TYPE_TIMESTAMP,0);ID3D12GraphicsCommandList_BeginQuery(list,stats_heap,D3D12_QUERY_TYPE_PIPELINE_STATISTICS,0);
 if(!getenv("IDENTITY_FIRST_SKIP_DRAW"))ID3D12GraphicsCommandList_DrawInstanced(list,6,1,0,0);
 ID3D12GraphicsCommandList_EndQuery(list,stats_heap,D3D12_QUERY_TYPE_PIPELINE_STATISTICS,0);ID3D12GraphicsCommandList_EndQuery(list,timestamps,D3D12_QUERY_TYPE_TIMESTAMP,1);
 ID3D12GraphicsCommandList_ResolveQueryData(list,timestamps,D3D12_QUERY_TYPE_TIMESTAMP,0,2,query_data,0);
 ID3D12GraphicsCommandList_ResolveQueryData(list,stats_heap,D3D12_QUERY_TYPE_PIPELINE_STATISTICS,0,1,query_data,16);
 transition(list,dest,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);
 image_copy.pResource=dest;buffer_copy.pResource=readback;ID3D12GraphicsCommandList_CopyTextureRegion(list,&buffer_copy,0,0,0,&image_copy,NULL);
 FIRST_OK(ID3D12GraphicsCommandList_Close(list));measurement->record_seconds=init_now()-before;
 before=init_now();submitted=(ID3D12CommandList*)list;ID3D12CommandQueue_ExecuteCommandLists(queue,1,&submitted);pending=1;
 if(!first_wait(queue,fence,event,2))goto done;pending=0;measurement->submit_complete_seconds=init_now()-before;
 before=init_now();range.End=16+sizeof(stats);FIRST_OK(ID3D12Resource_Map(query_data,0,&range,&mapped_query));
 memcpy(raw,mapped_query,sizeof(raw));memcpy(&stats,(const unsigned char*)mapped_query+16,sizeof(stats));range.Begin=range.End=0;ID3D12Resource_Unmap(query_data,0,&range);mapped_query=NULL;
 ticks[0]=raw[0];ticks[1]=1;ticks[2]=raw[1];ticks[3]=1;
 measurement->primitives=stats.IAPrimitives;measurement->vertices=stats.VSInvocations;measurement->fragments=stats.PSInvocations;measurement->pixels=w*h;
 range.End=bytes;FIRST_OK(ID3D12Resource_Map(readback,0,&range,&mapped_readback));
 for(y=0;y<h;++y){for(x=0;x<w;++x)if(((const uint32_t*)((const unsigned char*)mapped_readback+y*pitch))[x]!=first_pattern(x,y)){fprintf(stderr,"First transfer image mismatch %u,%u\n",x,y);goto done;}
  for(i=w*4;i<pitch;++i)if(((const unsigned char*)mapped_readback)[y*pitch+i]!=0x6d)goto done;}
 for(i=h*pitch;i<bytes;++i)if(((const unsigned char*)mapped_readback)[i]!=0x6d)goto done;
 if(stats.IAPrimitives!=2 || !stats.VSInvocations || stats.PSInvocations<(UINT64)w*h){fprintf(stderr,"First transfer work mismatch\n");goto done;}
 if(!xe_test_gpu_timestamp_seconds(ticks,64,period,measurement->submit_complete_seconds,&measurement->gpu_seconds))goto done;
 measurement->validation_seconds=init_now()-before;result=1;
done:
 if(pending){fprintf(stderr,"Pending first-transfer resources retained\n");exit(1);}
 if(mapped_query)ID3D12Resource_Unmap(query_data,0,NULL);if(mapped_readback)ID3D12Resource_Unmap(readback,0,NULL);
 if(mapped_upload)ID3D12Resource_Unmap(upload,0,NULL);if(mapped_guard)ID3D12Resource_Unmap(guard,0,NULL);
 if(stats_heap)ID3D12QueryHeap_Release(stats_heap);if(timestamps)ID3D12QueryHeap_Release(timestamps);
 if(event)CloseHandle(event);if(fence)ID3D12Fence_Release(fence);if(list)ID3D12GraphicsCommandList_Release(list);
 if(allocator)ID3D12CommandAllocator_Release(allocator);if(queue)ID3D12CommandQueue_Release(queue);
 if(srv_heap)ID3D12DescriptorHeap_Release(srv_heap);if(rtv_heap)ID3D12DescriptorHeap_Release(rtv_heap);
 if(query_data)ID3D12Resource_Release(query_data);if(readback)ID3D12Resource_Release(readback);
 if(guard)ID3D12Resource_Release(guard);if(upload)ID3D12Resource_Release(upload);
 if(dest)ID3D12Resource_Release(dest);if(source)ID3D12Resource_Release(source);return result;
}
