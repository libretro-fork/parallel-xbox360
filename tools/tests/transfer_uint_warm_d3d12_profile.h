/* Real legacy native-DXIL vs prepared-bank execution; test-only C89. */
#ifndef XE_TEST_TRANSFER_UINT_WARM_D3D12_PROFILE_H
#define XE_TEST_TRANSFER_UINT_WARM_D3D12_PROFILE_H
#include "src/xenia/gpu/dxil/edram_transfer.h"
#include "gpu_timestamp_measure.h"
#include "transfer_uint_d3d12_revision_load.h"
static const unsigned warm_pairs[10][2]={{4,6},{6,4},{5,7},{7,5},{14,4},{4,14},{15,5},{5,15},{4,5},{5,4}};
static int warm_pair(unsigned source,unsigned dest) {
 unsigned i;for(i=0;i<10;++i)if(warm_pairs[i][0]==source && warm_pairs[i][1]==dest)return (int)i;
 return -1;
}
static struct {
 ID3D12RootSignature *root;
 ID3D12PipelineState *legacy[10][3][3];
 ID3D12QueryHeap *timestamps,*statistics;
 ID3D12Resource *query_readback;
 ID3D12Fence *fence;HANDLE event;UINT64 serial,frequency;
 xe_test_d3d12_library revision;uint32_t parent_packet[28];unsigned parent_modules;size_t parent_bytes;
 FILE *csv;
} warm;
static int warm_parent_validate(void *context,void *data,size_t bytes) {
 if(!xe_test_validator_check(context,data,bytes,production_validation_report))return 0;
 ++warm.parent_modules;warm.parent_bytes+=bytes;return 1;
}
static int warm_parent_reject(void *context,void *data,size_t bytes) {
 (void)context;(void)data;(void)bytes;return 0;
}
static int warm_init(ID3D12Device *device,const D3D12_GRAPHICS_PIPELINE_STATE_DESC *base) {
 D3D12_ROOT_SIGNATURE_DESC root={0};D3D12_ROOT_PARAMETER parameters[2]={{0}};D3D12_DESCRIPTOR_RANGE range={0};
 D3D12_GRAPHICS_PIPELINE_STATE_DESC desc=*base;D3D12_QUERY_HEAP_DESC query={0};
 XeDxilEdramTransferOptions options={0};XeDxilShaderBinary pixel={0};
 ID3D12PipelineState *pipeline;unsigned p,m,s;HRESULT status;void *validator;
 const char *csv=getenv("TRANSFER_GPU_CSV"),*validator_path=getenv("XE_TEST_DXIL_VALIDATOR"),*parent=getenv("TRANSFER_PARENT_LIBRARY");
 double started,emit_seconds=0,pso_seconds=0;size_t bytes=0;
 if(!csv || !*csv || !validator_path || !*validator_path)return 0;
 if(parent) {
  xe_test_d3d12_config config={0};
  if(!xe_test_d3d12_load(&warm.revision,parent))return 0;
  validator=xe_test_validator_open(validator_path);if(!validator)return 0;
  config.device=device;config.vertex=base->VS.pShaderBytecode;config.vertex_bytes=base->VS.BytecodeLength;config.native2=1;
  config.validation_context=validator;config.validate=warm_parent_reject;config.scale_x=config.scale_y=1;
  /* A rejected first module must clean up and allow a subsequent real open. */
  {void *unexpected=warm.revision.api->open(&config);if(unexpected){warm.revision.api->close(unexpected);xe_test_validator_close(validator);return 0;}}
  config.validate=warm_parent_validate;
  for(s=0;s<3;++s) {
   config.scale_x=config.scale_y=1u<<s;
   warm.revision.owner[s]=warm.revision.api->open(&config);
   if(!warm.revision.owner[s]){xe_test_validator_close(validator);return 0;}
   warm.revision.root[s]=warm.revision.api->root(warm.revision.owner[s]);
   if(!warm.revision.root[s]){xe_test_validator_close(validator);return 0;}
   for(p=0;p<10;++p)for(m=0;m<3;++m) {
    xe_test_d3d12_key key={0};key.source_format=warm_pairs[p][0];key.dest_format=warm_pairs[p][1];key.source_msaa=key.dest_msaa=m;
    warm.legacy[p][m][s]=warm.revision.api->pipeline(warm.revision.owner[s],&key);
    if(!warm.legacy[p][m][s]){xe_test_validator_close(validator);return 0;}
   }
  }
  xe_test_validator_close(validator);
  if(!warm.parent_modules)return 0;
  fprintf(stderr,"D3D12_PARENT_LIBRARY abi%u kind%u packet%u validated_modules%u bytes%llu %s\n",warm.revision.api->version,warm.revision.api->kind,warm.revision.api->packet_bytes,warm.parent_modules,(unsigned long long)warm.parent_bytes,parent);
  goto queries;
 } range.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;range.NumDescriptors=1;
 parameters[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
 parameters[1].DescriptorTable.NumDescriptorRanges=1;parameters[1].DescriptorTable.pDescriptorRanges=&range;
 parameters[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
 parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
 parameters[0].Constants.ShaderRegister=1;parameters[0].Constants.RegisterSpace=31;parameters[0].Constants.Num32BitValues=4;
 parameters[1].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
 root.NumParameters=2;root.pParameters=parameters;root.Flags=D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
 if(FAILED(xe_transfer_uint_d3d12_native_ops()->create_root(device,&root,&warm.root)))return 0;
 validator=xe_test_validator_open(validator_path);if(!validator)return 0;
 desc.pRootSignature=warm.root;
 options.mode=1;options.source_uint=options.destination_uint=1;options.msaa_2x_supported=1;
 for(p=0;p<10;++p)for(m=0;m<3;++m)for(s=0;s<3;++s) {
  options.source_format=warm_pairs[p][0];options.destination_format=warm_pairs[p][1];
  options.source_msaa=options.destination_msaa=m;options.scale_x=options.scale_y=1u<<s;
  started=warm_now();
  if(!xe_dxil_build_edram_transfer(&options,&pixel))goto fail;
  emit_seconds+=warm_now()-started;bytes+=pixel.size;
  if(!xe_test_validator_check(validator,pixel.data,pixel.size,production_validation_report))goto fail;
  desc.PS.pShaderBytecode=pixel.data;desc.PS.BytecodeLength=pixel.size;
  desc.RTVFormats[0]=format(warm_pairs[p][1]);desc.SampleDesc.Count=1u<<m;desc.SampleMask=UINT32_MAX;
  pipeline=NULL;started=warm_now();
  status=ID3D12Device_CreateGraphicsPipelineState(device,&desc,&IID_ID3D12PipelineState,(void**)&pipeline);
  pso_seconds+=warm_now()-started;
  if(FAILED(status) || !pipeline)goto fail;
  warm.legacy[p][m][s]=pipeline;xe_dxil_shader_binary_free(&pixel);
 }
 xe_test_validator_close(validator);
 fprintf(stderr,"D3D12_LEGACY_INIT modules90 bytes%llu emission_seconds%.9g pso_seconds%.9g SDK_validated90\n",(unsigned long long)bytes,emit_seconds,pso_seconds);
queries:
 query.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;query.Count=2;
 if(FAILED(ID3D12Device_CreateQueryHeap(device,&query,&IID_ID3D12QueryHeap,(void**)&warm.timestamps)))return 0;
 query.Type=D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS;query.Count=1;
 if(FAILED(ID3D12Device_CreateQueryHeap(device,&query,&IID_ID3D12QueryHeap,(void**)&warm.statistics)))return 0;
 if(FAILED(buffer(device,D3D12_HEAP_TYPE_READBACK,16+sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS),&warm.query_readback)))return 0;
 if(FAILED(ID3D12Device_CreateFence(device,0,D3D12_FENCE_FLAG_NONE,&IID_ID3D12Fence,(void**)&warm.fence)))return 0;
 warm.event=CreateEventA(NULL,FALSE,FALSE,NULL);if(!warm.event)return 0;
 warm.csv=fopen(csv,"wb");if(!warm.csv)return 0;
 fprintf(warm.csv,"pair,msaa,scale,pixels,samples,repeat,trial,order,reference,start,available_start,end,available_end,bits,period_ns,host_seconds,gpu_seconds,ia_primitives,vs_invocations,fs_invocations,available_stats,parent\n");
 return 1;
fail:
 xe_dxil_shader_binary_free(&pixel);xe_test_validator_close(validator);return 0;
}
static void warm_close(void) {
 unsigned p,m,s;
 if(!warm.revision.api)for(p=0;p<10;++p)for(m=0;m<3;++m)for(s=0;s<3;++s)if(warm.legacy[p][m][s])ID3D12PipelineState_Release(warm.legacy[p][m][s]);
 if(!xe_test_d3d12_unload(&warm.revision))exit(1);
 if(warm.root)ID3D12RootSignature_Release(warm.root);
 if(warm.timestamps)ID3D12QueryHeap_Release(warm.timestamps);
 if(warm.statistics)ID3D12QueryHeap_Release(warm.statistics);
 if(warm.query_readback)ID3D12Resource_Release(warm.query_readback);
 if(warm.fence)ID3D12Fence_Release(warm.fence);
 if(warm.event)CloseHandle(warm.event);
 if(warm.csv)fclose(warm.csv);
 memset(&warm,0,sizeof(warm));
}
typedef struct warm_case {
 ID3D12CommandQueue *queue;ID3D12CommandAllocator *allocator;ID3D12GraphicsCommandList *list;
 ID3D12Resource *destination,*gpu_buffer,*readback,*guard;
 ID3D12DescriptorHeap *srv_heap;
 D3D12_VERTEX_BUFFER_VIEW vertex;
 D3D12_CPU_DESCRIPTOR_HANDLE rtv;
 D3D12_GPU_DESCRIPTOR_HANDLE source_srv,dest_srv;
 const bank *bank_;const xe_transfer_uint_d3d12_bank *ready;
 ID3D12PipelineState *pipeline;
 const xe_transfer_address_key *key;const xe_transfer_uint_packet *packet;
 unsigned pair,msaa,scale,width,height,bytes;
} warm_case;
#define WARM_HR(call) do {HRESULT warm_result=(call);if(FAILED(warm_result)){fprintf(stderr,"D3D12 warm line%u status%08lx\n",(unsigned)__LINE__,(unsigned long)warm_result);return 0;}}while(0)
static int warm_profile(const warm_case *c) {
 D3D12_VIEWPORT viewport={0};D3D12_RECT scissor;D3D12_RANGE range;
 D3D12_QUERY_DATA_PIPELINE_STATISTICS statistics;ID3D12CommandList *submitted;
 ID3D12PipelineState *pipeline;ID3D12RootSignature *root;
 uint64_t ticks[4],raw_ticks[2];void *mapped;const uint32_t *pixels;uint32_t wanted[4],constants[4];
 unsigned work_class,trial,order,reference,repeated,i,x,y,sample,lane,index,records;
 uint32_t legacy=c->key->dest_pitch|(c->key->source_pitch<<10)|(((uint32_t)c->key->dest_to_source_base&4095)<<20);
 const float clear[4]={7,7,7,7};
 double host_started,host_seconds,gpu_seconds;float period;
 if(warm.revision.api) {
  xe_test_d3d12_key key={0};key.source_format=warm_pairs[c->pair][0];key.dest_format=warm_pairs[c->pair][1];
  key.source_msaa=key.dest_msaa=c->msaa;key.source_pitch=c->key->source_pitch;key.dest_pitch=c->key->dest_pitch;key.delta=c->key->dest_to_source_base;
  if(!warm.revision.api->packet(warm.revision.owner[c->scale],&key,warm.parent_packet,warm.revision.api->packet_bytes))return 0;
 }
 if(!warm.frequency){WARM_HR(ID3D12CommandQueue_GetTimestampFrequency(c->queue,&warm.frequency));if(!warm.frequency)return 0;}
 period=(float)(1e9/(double)warm.frequency);viewport.Width=(float)c->width;viewport.Height=(float)c->height;viewport.MaxDepth=1;
 scissor.left=scissor.top=0;scissor.right=(LONG)c->width;scissor.bottom=(LONG)c->height;
 for(work_class=0;work_class<2;++work_class)for(trial=0;trial<8;++trial)for(order=0;order<2;++order) {
  reference=((order^(trial&1))==0)!=(getenv("TRANSFER_REVERSE_ORDER")!=NULL);repeated=work_class?512:64;
  root=reference?(warm.revision.api?warm.revision.root[c->scale]:warm.root):c->ready->root;pipeline=reference?warm.legacy[c->pair][c->msaa][c->scale]:c->pipeline;
  WARM_HR(ID3D12CommandAllocator_Reset(c->allocator));WARM_HR(ID3D12GraphicsCommandList_Reset(c->list,c->allocator,NULL));
  transition(c->list,c->destination,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);
  ID3D12GraphicsCommandList_ClearRenderTargetView(c->list,c->rtv,clear,0,NULL);
  ID3D12GraphicsCommandList_SetDescriptorHeaps(c->list,1,&c->srv_heap);
  ID3D12GraphicsCommandList_IASetVertexBuffers(c->list,0,1,&c->vertex);
  ID3D12GraphicsCommandList_IASetPrimitiveTopology(c->list,D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  ID3D12GraphicsCommandList_RSSetViewports(c->list,1,&viewport);ID3D12GraphicsCommandList_RSSetScissorRects(c->list,1,&scissor);
  ID3D12GraphicsCommandList_OMSetRenderTargets(c->list,1,&c->rtv,FALSE,NULL);
  ID3D12GraphicsCommandList_SetGraphicsRootSignature(c->list,root);
  ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(c->list,reference?(warm.revision.api?warm.revision.api->image_root:1):0,c->source_srv);
  if(reference)ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(c->list,
   warm.revision.api?warm.revision.api->constants_root:0,
   warm.revision.api?warm.revision.api->packet_bytes/4:1,
   warm.revision.api?warm.parent_packet:&legacy,0);
  else ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(c->list,1,28,c->packet,0);
  ID3D12GraphicsCommandList_SetPipelineState(c->list,pipeline);
  ID3D12GraphicsCommandList_EndQuery(c->list,warm.timestamps,D3D12_QUERY_TYPE_TIMESTAMP,0);
  ID3D12GraphicsCommandList_BeginQuery(c->list,warm.statistics,D3D12_QUERY_TYPE_PIPELINE_STATISTICS,0);
  for(i=0;i<repeated;++i)ID3D12GraphicsCommandList_DrawInstanced(c->list,6,1,0,0);
  ID3D12GraphicsCommandList_EndQuery(c->list,warm.statistics,D3D12_QUERY_TYPE_PIPELINE_STATISTICS,0);
  ID3D12GraphicsCommandList_EndQuery(c->list,warm.timestamps,D3D12_QUERY_TYPE_TIMESTAMP,1);
  ID3D12GraphicsCommandList_ResolveQueryData(c->list,warm.timestamps,D3D12_QUERY_TYPE_TIMESTAMP,0,2,warm.query_readback,0);
  ID3D12GraphicsCommandList_ResolveQueryData(c->list,warm.statistics,D3D12_QUERY_TYPE_PIPELINE_STATISTICS,0,1,warm.query_readback,16);
  transition(c->list,c->destination,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  /* First retained warmup validates BOTH outputs, outside the timed bracket. */
  if(!trial && !work_class) {
   transition(c->list,c->gpu_buffer,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_DEST);
   ID3D12GraphicsCommandList_CopyBufferRegion(c->list,c->gpu_buffer,0,c->guard,0,c->bytes);
   transition(c->list,c->gpu_buffer,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
   constants[0]=c->width;constants[1]=c->height;constants[2]=1u<<c->msaa;constants[3]=c->bytes/16;
   records=c->width*c->height*(1u<<c->msaa);
   ID3D12GraphicsCommandList_SetComputeRootSignature(c->list,c->bank_->utility_root);
   ID3D12GraphicsCommandList_SetPipelineState(c->list,c->bank_->probe[c->msaa!=0]);
   ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(c->list,0,c->dest_srv);
   ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(c->list,1,ID3D12Resource_GetGPUVirtualAddress(c->gpu_buffer));
   ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(c->list,2,4,constants,0);
   ID3D12GraphicsCommandList_Dispatch(c->list,(records+63)/64+1,1,1);
   transition(c->list,c->gpu_buffer,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
   ID3D12GraphicsCommandList_CopyBufferRegion(c->list,c->readback,0,c->gpu_buffer,0,c->bytes);
   transition(c->list,c->gpu_buffer,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  }
  WARM_HR(ID3D12GraphicsCommandList_Close(c->list));submitted=(ID3D12CommandList*)c->list;host_started=warm_now();
  ID3D12CommandQueue_ExecuteCommandLists(c->queue,1,&submitted);++warm.serial;
  /* Failed submission/timeout leaves GPU-owned storage retained by process exit. */
  if(FAILED(ID3D12CommandQueue_Signal(c->queue,warm.fence,warm.serial)) ||
     FAILED(ID3D12Fence_SetEventOnCompletion(warm.fence,warm.serial,warm.event)) ||
     WaitForSingleObject(warm.event,10000)!=WAIT_OBJECT_0){fprintf(stderr,"D3D12 warm pending work retained\n");exit(1);}
  host_seconds=warm_now()-host_started;
  if(ID3D12Fence_GetCompletedValue(warm.fence)<warm.serial)return 0;
  range.Begin=0;range.End=16+sizeof(statistics);mapped=NULL;
  WARM_HR(ID3D12Resource_Map(warm.query_readback,0,&range,&mapped));
  memcpy(raw_ticks,mapped,16);memcpy(&statistics,(const unsigned char*)mapped+16,sizeof(statistics));
  range.Begin=range.End=0;ID3D12Resource_Unmap(warm.query_readback,0,&range);
  ticks[0]=raw_ticks[0];ticks[1]=1;ticks[2]=raw_ticks[1];ticks[3]=1;
  if(!xe_test_gpu_timestamp_seconds(ticks,64,period,host_seconds,&gpu_seconds) || !(gpu_seconds>0))return 0;
  if(statistics.IAPrimitives!=(UINT64)2*repeated || !statistics.VSInvocations ||
     statistics.PSInvocations<(UINT64)c->width*c->height*(UINT64_C(1)<<c->msaa)*repeated){fprintf(stderr,"D3D12 warm work mismatch pair%u msaa%u scale%u IA%llu VS%llu PS%llu\n",c->pair,c->msaa,1u<<c->scale,(unsigned long long)statistics.IAPrimitives,(unsigned long long)statistics.VSInvocations,(unsigned long long)statistics.PSInvocations);return 0;}
  if(!trial && !work_class) {
   range.Begin=0;range.End=c->bytes;mapped=NULL;WARM_HR(ID3D12Resource_Map(c->readback,0,&range,&mapped));pixels=(const uint32_t*)mapped;
   for(y=0;y<c->height;++y)for(x=0;x<c->width;++x)for(sample=0;sample<(1u<<c->msaa);++sample) {
    image_ms_expected(c->key,warm_pairs[c->pair][0],warm_pairs[c->pair][1],x,y,sample,wanted);
    index=((y*c->width+x)*(1u<<c->msaa)+sample)*4;
    for(lane=0;lane<image_components(warm_pairs[c->pair][1]);++lane)if(pixels[index+lane]!=wanted[lane]){
     fprintf(stderr,"D3D12 warm image mismatch pair%u msaa%u scale%u reference%u pixel%u,%u sample%u lane%u\n",c->pair,c->msaa,1u<<c->scale,reference,x,y,sample,lane);
     range.Begin=range.End=0;ID3D12Resource_Unmap(c->readback,0,&range);return 0;
    }
   }
   records=c->width*c->height*(1u<<c->msaa);
   for(i=records*4;i<c->bytes/4;++i)if(pixels[i]!=0x6d6d6d6du){range.Begin=range.End=0;ID3D12Resource_Unmap(c->readback,0,&range);return 0;}
   range.Begin=range.End=0;ID3D12Resource_Unmap(c->readback,0,&range);
  }
  fprintf(warm.csv,"%u,%u,%u,%u,%u,%u,%u,%u,%u,%llu,1,%llu,1,64,%.17g,%.17g,%.17g,%llu,%llu,%llu,1,%u\n",c->pair,c->msaa,1u<<c->scale,c->width*c->height,1u<<c->msaa,repeated,trial,order,reference,(unsigned long long)ticks[0],(unsigned long long)ticks[2],(double)period,host_seconds,gpu_seconds,(unsigned long long)statistics.IAPrimitives,(unsigned long long)statistics.VSInvocations,(unsigned long long)statistics.PSInvocations,(unsigned)(warm.revision.api!=NULL));
 }
 return fflush(warm.csv)==0;
}
#endif
