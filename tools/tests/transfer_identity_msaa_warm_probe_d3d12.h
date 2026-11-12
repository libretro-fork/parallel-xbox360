/* Independent untimed reset/probe; borrow mapped results directly. BSD; MSVC C89. */
typedef struct identity_msaa_probe_context {
 ID3D12CommandAllocator *allocator;ID3D12GraphicsCommandList *list;ID3D12CommandQueue *queue;
 ID3D12DescriptorHeap *heap;ID3D12Resource *typed,*oracle,*guard,*gpu,*readback;
 D3D12_CPU_DESCRIPTOR_HANDLE rtv;D3D12_GPU_DESCRIPTOR_HANDLE srv;
 const bank *bank_;const xe_transfer_address_key *key;
 unsigned width,height,samples,view,bytes;
} identity_msaa_probe_context;
#define IMP_OK(x) do {if(FAILED(x)){fprintf(stderr,"MSAA probe API failure line%u\n",(unsigned)__LINE__);return 0;}}while(0)
static int identity_msaa_submit(identity_msaa_probe_context *c) {
 ID3D12CommandList *submitted=(ID3D12CommandList*)c->list;UINT64 completed;
 IMP_OK(ID3D12GraphicsCommandList_Close(c->list));
 ID3D12CommandQueue_ExecuteCommandLists(c->queue,1,&submitted);++idw.serial;
 if(FAILED(ID3D12CommandQueue_Signal(c->queue,idw.fence,idw.serial)) ||
    FAILED(ID3D12Fence_SetEventOnCompletion(idw.fence,idw.serial,idw.event)) ||
    WaitForSingleObject(idw.event,10000)!=WAIT_OBJECT_0) {
  fprintf(stderr,"Pending MSAA probe resources retained\n");exit(1);
 }
 completed=ID3D12Fence_GetCompletedValue(idw.fence);
 return completed!=UINT64_MAX && completed>=idw.serial;
}
static int identity_msaa_prepare(void *opaque) {
 identity_msaa_probe_context *c=(identity_msaa_probe_context*)opaque;float clear[4];unsigned lane;
 IMP_OK(ID3D12CommandAllocator_Reset(c->allocator));
 IMP_OK(ID3D12GraphicsCommandList_Reset(c->list,c->allocator,NULL));
 transition(c->list,c->oracle,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);
 for(lane=0;lane<4;++lane)clear[lane]=(float)((7&(lane==3?identity_ms_alpha_mask(c->view):identity_ms_mask(c->view)))|identity_ms_base(c->view));
 ID3D12GraphicsCommandList_ClearRenderTargetView(c->list,c->rtv,clear,0,NULL);
 transition(c->list,c->oracle,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);
 transition(c->list,c->typed,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
 ID3D12GraphicsCommandList_CopyResource(c->list,c->typed,c->oracle);
 transition(c->list,c->typed,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_SOURCE);
 transition(c->list,c->oracle,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
 return identity_msaa_submit(c);
}
static int identity_msaa_probe(void *opaque) {
 identity_msaa_probe_context *c=(identity_msaa_probe_context*)opaque;
 unsigned records,gx,gy,x,y,s,lane,index;uint32_t constants[4],wanted[4],*actual;
 D3D12_RANGE range={0};void *mapped=NULL;int result=1;
 if((uint64_t)c->width*c->height*c->samples>c->bytes/16)return 0;
 records=c->width*c->height*c->samples;gx=(records+63)/64+1;if(gx>65535)gx=65535;
 gy=((records+63)/64+1+gx-1)/gx;
 constants[0]=c->width;constants[1]=c->height;constants[2]=c->samples;constants[3]=gx*64;
 IMP_OK(ID3D12CommandAllocator_Reset(c->allocator));
 IMP_OK(ID3D12GraphicsCommandList_Reset(c->list,c->allocator,NULL));
 transition(c->list,c->oracle,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
 ID3D12GraphicsCommandList_CopyResource(c->list,c->oracle,c->typed);
 transition(c->list,c->oracle,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
 transition(c->list,c->gpu,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_DEST);
 ID3D12GraphicsCommandList_CopyBufferRegion(c->list,c->gpu,0,c->guard,0,c->bytes);
 transition(c->list,c->gpu,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
 ID3D12GraphicsCommandList_SetDescriptorHeaps(c->list,1,&c->heap);
 ID3D12GraphicsCommandList_SetComputeRootSignature(c->list,c->bank_->utility_root);
 ID3D12GraphicsCommandList_SetPipelineState(c->list,c->bank_->probe[c->samples!=1]);
 ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(c->list,0,c->srv);
 ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(c->list,1,ID3D12Resource_GetGPUVirtualAddress(c->gpu));
 ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(c->list,2,4,constants,0);
 ID3D12GraphicsCommandList_Dispatch(c->list,gx,gy,1);
 transition(c->list,c->gpu,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
 ID3D12GraphicsCommandList_CopyBufferRegion(c->list,c->readback,0,c->gpu,0,c->bytes);
 transition(c->list,c->gpu,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
 if(!identity_msaa_submit(c))return 0;
 range.End=c->bytes;IMP_OK(ID3D12Resource_Map(c->readback,0,&range,&mapped));actual=(uint32_t*)mapped;
 for(y=0;y<c->height;++y)for(x=0;x<c->width;++x)for(s=0;s<c->samples;++s) {
  identity_ms_expected(c->key,c->view,x,y,s,wanted);index=((y*c->width+x)*c->samples+s)*4;
  for(lane=0;lane<4;++lane)if(actual[index+lane]!=wanted[lane]) {
   fprintf(stderr,"MSAA timed-batch image mismatch view%u source%u dest%u native%u pixel%u,%u sample%u lane%u\n",
    c->view,c->key->source_msaa,c->key->dest_msaa,c->key->dest_native2,x,y,s,lane);
   result=0;goto done;
  }
 }
 for(index=records*4;index<c->bytes/4;++index)if(actual[index]!=0x6d6d6d6du){result=0;break;}
done:
 range.Begin=range.End=0;ID3D12Resource_Unmap(c->readback,0,&range);return result;
}
