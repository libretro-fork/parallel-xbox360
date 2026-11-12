/* Matched typed-resource numeric transfer measurement only. BSD; MSVC C89. */
#include "gpu_timestamp_measure.h"
#include "shader_compile_bench.h"
#ifndef XE_SHADER_COMPILE_BENCH
#error Build the standalone measurement tool with XE_SHADER_COMPILE_BENCH
#endif
static struct {
 ID3D12QueryHeap *timestamps,*stats;ID3D12Resource *readback;
 ID3D12Fence *fence;HANDLE event;UINT64 serial,frequency;FILE *csv;
} idw;
static int identity_warm_init(ID3D12Device *device,ID3D12CommandQueue *queue)
{
 D3D12_QUERY_HEAP_DESC query={0};const char *csv=getenv("IDENTITY_GPU_CSV");
 if(!csv || FAILED(ID3D12CommandQueue_GetTimestampFrequency(queue,&idw.frequency)) || !idw.frequency)return 0;
 query.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;query.Count=2;
 if(FAILED(ID3D12Device_CreateQueryHeap(device,&query,&IID_ID3D12QueryHeap,(void**)&idw.timestamps)))return 0;
 query.Type=D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS;query.Count=1;
 if(FAILED(ID3D12Device_CreateQueryHeap(device,&query,&IID_ID3D12QueryHeap,(void**)&idw.stats)))return 0;
 if(FAILED(buffer(device,D3D12_HEAP_TYPE_READBACK,16+sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS),&idw.readback)))return 0;
 if(FAILED(ID3D12Device_CreateFence(device,0,D3D12_FENCE_FLAG_NONE,&IID_ID3D12Fence,(void**)&idw.fence)))return 0;
 idw.event=CreateEventA(NULL,FALSE,FALSE,NULL);if(!idw.event)return 0;
 idw.csv=fopen(csv,"wb");if(!idw.csv)return 0;
 return fprintf(idw.csv,"view,scale,pixels,repeat,trial,order,reference,start,available_start,end,available_end,bits,period_ns,host_seconds,gpu_seconds,ia_primitives,vs_invocations,fs_invocations,available_stats,work_multiplier\n")>0;
}
static int identity_warm_close(void)
{
 int result=1;
 if(idw.csv)result=fclose(idw.csv)==0;
 if(idw.event)CloseHandle(idw.event);
 if(idw.fence)ID3D12Fence_Release(idw.fence);
 if(idw.readback)ID3D12Resource_Release(idw.readback);
 if(idw.timestamps)ID3D12QueryHeap_Release(idw.timestamps);
 if(idw.stats)ID3D12QueryHeap_Release(idw.stats);
 memset(&idw,0,sizeof(idw));return result;
}
typedef struct identity_warm_case {
 ID3D12CommandAllocator *allocator;ID3D12GraphicsCommandList *list;
 ID3D12CommandQueue *queue;ID3D12DescriptorHeap *heap;ID3D12Resource *destination;
 D3D12_CPU_DESCRIPTOR_HANDLE rtv;D3D12_GPU_DESCRIPTOR_HANDLE srv;
 D3D12_VERTEX_BUFFER_VIEW vertices;unsigned width,height,view,scale;
 ID3D12PipelineState *pipeline[2];ID3D12RootSignature *root[2];
 const xe_transfer_identity_packet *packet;uint32_t packed;
} identity_warm_case;
#define IDW_OK(x) do {if(FAILED(x)){fprintf(stderr,"Numeric D3D12 GPU API failure line%u\n",(unsigned)__LINE__);return 0;}}while(0)
static int identity_warm_profile(const identity_warm_case *c)
{
 D3D12_VIEWPORT viewport={0};D3D12_RECT scissor={0};D3D12_RANGE range={0};
 D3D12_QUERY_DATA_PIPELINE_STATISTICS stats;ID3D12CommandList *submitted;void *mapped;
 UINT64 raw[2];uint64_t ticks[4];unsigned trial,order,reference,work,repeated,draw,multiplier;
 double started,host,seconds;float period=(float)(1e9/(double)idw.frequency);
 viewport.Width=(float)c->width;viewport.Height=(float)c->height;viewport.MaxDepth=1;
 scissor.right=(LONG)c->width;scissor.bottom=(LONG)c->height;
 for(work=0;work<2;++work)for(trial=0;trial<8;++trial)for(order=0;order<2;++order) {
  reference=((order^(trial&1))==0)!=(getenv("IDENTITY_REVERSE_ORDER")!=NULL);
  repeated=work?512:64;multiplier=!reference && getenv("IDENTITY_EXTRA_WORK")?2:1;
  IDW_OK(ID3D12CommandAllocator_Reset(c->allocator));
  IDW_OK(ID3D12GraphicsCommandList_Reset(c->list,c->allocator,c->pipeline[reference]));
  transition(c->list,c->destination,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);
  ID3D12GraphicsCommandList_SetGraphicsRootSignature(c->list,c->root[reference]);
  ID3D12GraphicsCommandList_SetDescriptorHeaps(c->list,1,&c->heap);
  ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(c->list,0,c->srv);
  ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(c->list,1,reference?1:5,
   reference?(const void*)&c->packed:(const void*)c->packet,0);
  ID3D12GraphicsCommandList_RSSetViewports(c->list,1,&viewport);
  ID3D12GraphicsCommandList_RSSetScissorRects(c->list,1,&scissor);
  ID3D12GraphicsCommandList_OMSetRenderTargets(c->list,1,&c->rtv,FALSE,NULL);
  ID3D12GraphicsCommandList_IASetVertexBuffers(c->list,0,1,&c->vertices);
  ID3D12GraphicsCommandList_IASetPrimitiveTopology(c->list,D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  ID3D12GraphicsCommandList_EndQuery(c->list,idw.timestamps,D3D12_QUERY_TYPE_TIMESTAMP,0);
  ID3D12GraphicsCommandList_BeginQuery(c->list,idw.stats,D3D12_QUERY_TYPE_PIPELINE_STATISTICS,0);
  for(draw=0;draw<repeated*multiplier;++draw)ID3D12GraphicsCommandList_DrawInstanced(c->list,6,1,0,0);
  ID3D12GraphicsCommandList_EndQuery(c->list,idw.stats,D3D12_QUERY_TYPE_PIPELINE_STATISTICS,0);
  ID3D12GraphicsCommandList_EndQuery(c->list,idw.timestamps,D3D12_QUERY_TYPE_TIMESTAMP,1);
  ID3D12GraphicsCommandList_ResolveQueryData(c->list,idw.timestamps,D3D12_QUERY_TYPE_TIMESTAMP,0,2,idw.readback,0);
  ID3D12GraphicsCommandList_ResolveQueryData(c->list,idw.stats,D3D12_QUERY_TYPE_PIPELINE_STATISTICS,0,1,idw.readback,16);
  transition(c->list,c->destination,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);
  IDW_OK(ID3D12GraphicsCommandList_Close(c->list));submitted=(ID3D12CommandList*)c->list;
  started=xe_compile_now();ID3D12CommandQueue_ExecuteCommandLists(c->queue,1,&submitted);++idw.serial;
  if(FAILED(ID3D12CommandQueue_Signal(c->queue,idw.fence,idw.serial)) ||
   FAILED(ID3D12Fence_SetEventOnCompletion(idw.fence,idw.serial,idw.event)) ||
   WaitForSingleObject(idw.event,10000)!=WAIT_OBJECT_0) {
   fprintf(stderr,"Pending numeric D3D12 benchmark work retained\n");exit(1);
  }
  host=xe_compile_now()-started;
  if(ID3D12Fence_GetCompletedValue(idw.fence)<idw.serial)return 0;
  range.Begin=0;range.End=16+sizeof(stats);mapped=NULL;
  IDW_OK(ID3D12Resource_Map(idw.readback,0,&range,&mapped));
  memcpy(raw,mapped,sizeof(raw));memcpy(&stats,(const unsigned char*)mapped+16,sizeof(stats));
  range.Begin=range.End=0;ID3D12Resource_Unmap(idw.readback,0,&range);
  ticks[0]=raw[0];ticks[1]=1;ticks[2]=raw[1];ticks[3]=1;
  if(!xe_test_gpu_timestamp_seconds(ticks,64,period,host,&seconds))return 0;
  if(stats.IAPrimitives!=(UINT64)2*repeated*multiplier || !stats.VSInvocations ||
   stats.PSInvocations<(UINT64)c->width*c->height*repeated*multiplier)return 0;
  fprintf(idw.csv,"%u,%u,%u,%u,%u,%u,%u,%llu,1,%llu,1,64,%.17g,%.17g,%.17g,%llu,%llu,%llu,1,%u\n",
   c->view,c->scale,c->width*c->height,repeated,trial,order,reference,
   (unsigned long long)ticks[0],(unsigned long long)ticks[2],(double)period,host,seconds,
   (unsigned long long)stats.IAPrimitives,(unsigned long long)stats.VSInvocations,
   (unsigned long long)stats.PSInvocations,multiplier);
 }
 return fflush(idw.csv)==0;
}
