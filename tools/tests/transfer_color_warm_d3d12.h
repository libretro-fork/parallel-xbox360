/* Test-only paired GPU work. All objects are borrowed through the final fence.
 * Query storage is allocated before recording; no validation/copies/compilation
 * occur between timestamp endpoints. Not a cold-cache or gameplay benchmark. */
#ifndef XE_TEST_COLOR_WARM_D3D12_H
#define XE_TEST_COLOR_WARM_D3D12_H
#include <inttypes.h>

typedef struct color_warm_context {
 ID3D12QueryHeap *timestamps,*statistics;
 ID3D12Resource *readback;
 HANDLE csv;
 UINT64 frequency;
 unsigned enabled,hardware,control,same,reverse;
} color_warm_context;
static HANDLE color_warm_csv=INVALID_HANDLE_VALUE;

static int color_warm_write(color_warm_context *w,const char *text)
{
 DWORD written=0;size_t length=strlen(text);
 return length<=UINT32_MAX && WriteFile(w->csv,text,(DWORD)length,&written,NULL) && written==length;
}
static void color_warm_close(color_warm_context *w)
{
 if(w->readback)ID3D12Resource_Release(w->readback);
 if(w->statistics)ID3D12QueryHeap_Release(w->statistics);
 if(w->timestamps)ID3D12QueryHeap_Release(w->timestamps);
 memset(w,0,sizeof(*w));
}
static int color_warm_finish(void)
{
 BOOL flushed,closed;
 if(color_warm_csv==INVALID_HANDLE_VALUE)return 1;
 flushed=FlushFileBuffers(color_warm_csv);closed=CloseHandle(color_warm_csv);
 color_warm_csv=INVALID_HANDLE_VALUE;return flushed && closed;
}
static unsigned color_warm_flag(const char *name)
{
 const char *value=getenv(name);
 if(!value || !*value)return 0;
 if(!strcmp(value,"1"))return 1;
 fprintf(stderr,"invalid color warm flag %s\n",name);ExitProcess(2);
}
static int color_warm_open(color_warm_context *w,ID3D12Device *device,ID3D12CommandQueue *queue,int hardware)
{
 const char *path=getenv("XE_COLOR_D3D12_WARM_CSV");
 D3D12_QUERY_HEAP_DESC desc={0};
 if(!path || !*path)return 1;
 w->hardware=hardware!=0;w->control=color_warm_flag("XE_COLOR_WARM_CONTROL");
 w->same=color_warm_flag("XE_COLOR_WARM_SAME_PIPELINE");w->reverse=color_warm_flag("XE_COLOR_WARM_REVERSE");
 /* Full performance runs require a physical device. Small controls never
  * qualify throughput, even if they happen to execute on that device. */
 if(!w->hardware && !w->control)return 0;
 if(color_warm_csv==INVALID_HANDLE_VALUE){
  color_warm_csv=CreateFileA(path,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
  if(color_warm_csv==INVALID_HANDLE_VALUE)return 0;
  w->csv=color_warm_csv;
  if(!color_warm_write(w,"hardware,control,self_check,reverse,scale_x,scale_y,native_class,source,dest,value_convert,source_width,source_height,source_pitch,dest_pitch,width,height,repeat,trial,order,reference,frequency,start,end,host_seconds,ia_vertices,ia_primitives,vs_invocations,ps_invocations\n"))return 0;
 }else w->csv=color_warm_csv;
 desc.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;desc.Count=64;
 if(FAILED(ID3D12Device_CreateQueryHeap(device,&desc,&IID_ID3D12QueryHeap,(void**)&w->timestamps)))return 0;
 desc.Type=D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS;desc.Count=32;
 if(FAILED(ID3D12Device_CreateQueryHeap(device,&desc,&IID_ID3D12QueryHeap,(void**)&w->statistics)))return 0;
 if(FAILED(color_resource(device,0,4096,0,D3D12_HEAP_TYPE_READBACK,0,&w->readback)))return 0;
 if(FAILED(ID3D12CommandQueue_GetTimestampFrequency(queue,&w->frequency)) || !w->frequency)return 0;
 w->enabled=1;return 1;
}
static double color_warm_now(void)
{
 LARGE_INTEGER ticks,frequency;
 if(!QueryPerformanceCounter(&ticks) || !QueryPerformanceFrequency(&frequency) || frequency.QuadPart<=0)ExitProcess(2);
 return (double)ticks.QuadPart/(double)frequency.QuadPart;
}
static int color_warm_case(color_warm_context *w,ID3D12CommandQueue *queue,
 ID3D12CommandAllocator *allocator,ID3D12GraphicsCommandList *list,ID3D12Fence *fence,HANDLE event,UINT64 *serial,
 const xe_transfer_color_d3d12_config *config,ID3D12DescriptorHeap *srv_heap,D3D12_GPU_DESCRIPTOR_HANDLE srv_gpu,
 D3D12_CPU_DESCRIPTOR_HANDLE rtv,const D3D12_VERTEX_BUFFER_VIEW *vertex,const D3D12_VIEWPORT *viewport,
 const D3D12_RECT *scissor,const unsigned words[4],ID3D12Resource *dest,ID3D12PipelineState *prepared,
 ID3D12PipelineState *legacy,unsigned source_width,unsigned source_height,unsigned native_class,unsigned source_format,unsigned dest_format,unsigned converted)
{
 unsigned workload,trial,order,index=0,reference,repeat,draw,repeats[32],roles[32],trials[32],orders[32],measurements;
 void *mapped=NULL;UINT64 *ticks;D3D12_QUERY_DATA_PIPELINE_STATISTICS *stats;
 D3D12_RANGE range={0,4096};ID3D12CommandList *submitted;
 double started,host;char row[1024];int pending=0,result=0;
 if(!w->enabled)return 1;
 if(w->control && (config->scale_x!=1 || config->scale_y!=1 || native_class || source_format || dest_format!=1 || converted))return 1;
#define WARM_HR(x) do{if(FAILED(x))goto done;}while(0)
 WARM_HR(ID3D12CommandAllocator_Reset(allocator));
 WARM_HR(ID3D12GraphicsCommandList_Reset(list,allocator,NULL));
 color_transition(list,dest,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);
 ID3D12GraphicsCommandList_SetGraphicsRootSignature(list,config->root);
 ID3D12GraphicsCommandList_SetDescriptorHeaps(list,1,&srv_heap);
 ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(list,1,srv_gpu);
 ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(list,0,4,words,0);
 ID3D12GraphicsCommandList_RSSetViewports(list,1,viewport);ID3D12GraphicsCommandList_RSSetScissorRects(list,1,scissor);
 ID3D12GraphicsCommandList_OMSetRenderTargets(list,1,&rtv,FALSE,NULL);
 ID3D12GraphicsCommandList_IASetVertexBuffers(list,0,1,vertex);
 ID3D12GraphicsCommandList_IASetPrimitiveTopology(list,D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
 for(workload=0;workload<2;++workload)for(trial=0;trial<(w->control?2u:8u);++trial)for(order=0;order<2;++order){
  reference=((order^(trial&1))==0)?1u:0u;reference^=w->reverse;
  repeat=w->control?(workload?8u:2u):(workload?512u:64u);
  repeats[index]=repeat;roles[index]=reference;trials[index]=trial;orders[index]=order;
  ID3D12GraphicsCommandList_SetPipelineState(list,(reference || w->same)?legacy:prepared);
  ID3D12GraphicsCommandList_BeginQuery(list,w->statistics,D3D12_QUERY_TYPE_PIPELINE_STATISTICS,index);
  ID3D12GraphicsCommandList_EndQuery(list,w->timestamps,D3D12_QUERY_TYPE_TIMESTAMP,index*2);
  for(draw=0;draw<repeat;++draw){
#ifdef XE_COLOR_WARM_MISSING_WORK
   if(!reference && draw+1==repeat)continue;
#endif
   ID3D12GraphicsCommandList_DrawInstanced(list,6,1,0,0);
  }
  ID3D12GraphicsCommandList_EndQuery(list,w->timestamps,D3D12_QUERY_TYPE_TIMESTAMP,index*2+1);
  ID3D12GraphicsCommandList_EndQuery(list,w->statistics,D3D12_QUERY_TYPE_PIPELINE_STATISTICS,index);
  ++index;
 }
 measurements=index;
 ID3D12GraphicsCommandList_ResolveQueryData(list,w->timestamps,D3D12_QUERY_TYPE_TIMESTAMP,0,measurements*2,w->readback,0);
 ID3D12GraphicsCommandList_ResolveQueryData(list,w->statistics,D3D12_QUERY_TYPE_PIPELINE_STATISTICS,0,measurements,w->readback,512);
 WARM_HR(ID3D12GraphicsCommandList_Close(list));submitted=(ID3D12CommandList*)list;
 started=color_warm_now();ID3D12CommandQueue_ExecuteCommandLists(queue,1,&submitted);pending=1;
 ++*serial;WARM_HR(ID3D12CommandQueue_Signal(queue,fence,*serial));
 WARM_HR(ID3D12Fence_SetEventOnCompletion(fence,*serial,event));
 if(WaitForSingleObject(event,30000)!=WAIT_OBJECT_0 || ID3D12Fence_GetCompletedValue(fence)<*serial)goto done;
 pending=0;host=color_warm_now()-started;
 WARM_HR(ID3D12Resource_Map(w->readback,0,&range,&mapped));ticks=(UINT64*)mapped;
 stats=(D3D12_QUERY_DATA_PIPELINE_STATISTICS*)((unsigned char*)mapped+512);
 for(index=0;index<measurements;++index){
  UINT64 minimum=(UINT64)(unsigned)viewport->Width*(unsigned)viewport->Height*repeats[index];
  if(ticks[index*2+1]<=ticks[index*2] || stats[index].IAVertices!=6u*(UINT64)repeats[index] ||
   stats[index].IAPrimitives!=2u*(UINT64)repeats[index] || !stats[index].VSInvocations || stats[index].PSInvocations<minimum){
   fprintf(stderr,"color warm missing GPU work or timestamps case%u/%u class%u measurement%u\n",source_format,dest_format,native_class,index);goto done;
  }
  sprintf(row,"%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%.17g,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 "\n",
   w->hardware,w->control,w->same,w->reverse,config->scale_x,config->scale_y,native_class,source_format,dest_format,converted,
   source_width,source_height,(words[0]>>10)&1023u,words[0]&1023u,
   (unsigned)viewport->Width,(unsigned)viewport->Height,repeats[index],trials[index],orders[index],roles[index],
   w->frequency,ticks[index*2],ticks[index*2+1],host,stats[index].IAVertices,stats[index].IAPrimitives,stats[index].VSInvocations,stats[index].PSInvocations);
  if(!color_warm_write(w,row))goto done;
 }
 result=1;
done:
 if(pending){fprintf(stderr,"Pending color-warm GPU resources retained on failure\n");ExitProcess(3);}
 if(mapped)ID3D12Resource_Unmap(w->readback,0,NULL);
 return result;
#undef WARM_HR
}
#endif
