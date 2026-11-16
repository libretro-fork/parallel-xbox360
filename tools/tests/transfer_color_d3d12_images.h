/* Native prepared/legacy PSO image parity, not an independent codec oracle.
 * All synchronization, readback and allocations below are test-only. C89. */
#ifndef XE_TEST_COLOR_D3D12_IMAGES_H
#define XE_TEST_COLOR_D3D12_IMAGES_H
static unsigned color_pixel_bytes(unsigned view)
{return view==1 || view==3 || view==5 || view==7?8:4;}
static HRESULT color_resource(ID3D12Device *device,unsigned view,unsigned width,unsigned height,
 D3D12_HEAP_TYPE heap_type,int target,ID3D12Resource **out)
{
 D3D12_HEAP_PROPERTIES heap={0};D3D12_RESOURCE_DESC desc={0};
 heap.Type=heap_type;desc.Height=desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;
 if(height){desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=width;desc.Height=height;
  desc.Format=xe_transfer_color_d3d12_view(view);desc.Flags=target?D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET:0;}
 else{desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=width;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;}
 return ID3D12Device_CreateCommittedResource(device,&heap,D3D12_HEAP_FLAG_NONE,&desc,
  heap_type==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:
   target?D3D12_RESOURCE_STATE_RENDER_TARGET:D3D12_RESOURCE_STATE_COPY_DEST,NULL,&IID_ID3D12Resource,(void**)out);
}
static void color_transition(ID3D12GraphicsCommandList *list,ID3D12Resource *resource,
 D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after)
{
 D3D12_RESOURCE_BARRIER barrier={0};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
 barrier.Transition.pResource=resource;barrier.Transition.StateBefore=before;barrier.Transition.StateAfter=after;
 barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
 ID3D12GraphicsCommandList_ResourceBarrier(list,1,&barrier);
}
static int color_images(ID3D12Device *device,const xe_transfer_color_d3d12_config *config,
 const xe_transfer_color_d3d12_bank *bank)
{
 enum{BUFFER_BYTES=1048576,VERTEX_OFFSET=1048544,REFERENCE_OFFSET=524288};
 static const unsigned formats[12]={0,1,2,3,4,5,14,15,6,7,10,12};
 static const unsigned classes[12]={0,1,2,3,4,5,6,7,4,5,2,3};
 ID3D12CommandQueue *queue=NULL;ID3D12CommandAllocator *allocator=NULL;
 ID3D12GraphicsCommandList *list=NULL;ID3D12CommandList *submitted;
 ID3D12DescriptorHeap *srv_heap=NULL,*rtv_heap=NULL;ID3D12Resource *upload=NULL,*readback=NULL,*source=NULL,*dest=NULL;
 ID3D12PipelineState *legacy=NULL,*const *prepared;ID3D12Fence *fence=NULL;HANDLE event=NULL;
 D3D12_COMMAND_QUEUE_DESC queue_desc={0};D3D12_DESCRIPTOR_HEAP_DESC heap_desc={0};
 D3D12_CPU_DESCRIPTOR_HANDLE srv_cpu,rtv;D3D12_GPU_DESCRIPTOR_HANDLE srv_gpu;
 D3D12_SHADER_RESOURCE_VIEW_DESC srv={0};D3D12_GRAPHICS_PIPELINE_STATE_DESC desc={0};
 D3D12_INPUT_ELEMENT_DESC input={0};D3D12_VERTEX_BUFFER_VIEW vertex;
 D3D12_TEXTURE_COPY_LOCATION image={0},buffer={0};D3D12_RANGE range={0};
 D3D12_VIEWPORT viewport={0};D3D12_RECT scissor={0};
 XeDxilEdramTransferOptions options={0};XeDxilShaderBinary binary={0};
 void *mapped=NULL,*samples=NULL;unsigned a,b,s,d,scale,converted,x,y,variant,sw,sh,dw,dh,sp,dp,count=0,words[4]={0};
 uint32_t p;HRESULT status=E_FAIL;int result=0,pending=0;
#define COLOR_HR(x) do{status=(x);if(FAILED(status)){fprintf(stderr,"color image line%u HRESULT%08lx\n",(unsigned)__LINE__,(unsigned long)status);goto done;}}while(0)
 COLOR_HR(ID3D12Device_CreateCommandQueue(device,&queue_desc,&IID_ID3D12CommandQueue,(void**)&queue));
 COLOR_HR(ID3D12Device_CreateCommandAllocator(device,D3D12_COMMAND_LIST_TYPE_DIRECT,&IID_ID3D12CommandAllocator,(void**)&allocator));
 COLOR_HR(ID3D12Device_CreateCommandList(device,0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator,NULL,&IID_ID3D12GraphicsCommandList,(void**)&list));
 COLOR_HR(ID3D12GraphicsCommandList_Close(list));
 COLOR_HR(ID3D12Device_CreateFence(device,0,D3D12_FENCE_FLAG_NONE,&IID_ID3D12Fence,(void**)&fence));
 event=CreateEventA(NULL,FALSE,FALSE,NULL);if(!event)goto done;
 heap_desc.NumDescriptors=1;heap_desc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;heap_desc.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
 COLOR_HR(ID3D12Device_CreateDescriptorHeap(device,&heap_desc,&IID_ID3D12DescriptorHeap,(void**)&srv_heap));
 ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(srv_heap,&srv_cpu);
 ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(srv_heap,&srv_gpu);
 heap_desc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;heap_desc.Flags=0;
 COLOR_HR(ID3D12Device_CreateDescriptorHeap(device,&heap_desc,&IID_ID3D12DescriptorHeap,(void**)&rtv_heap));
 ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(rtv_heap,&rtv);
 COLOR_HR(color_resource(device,0,BUFFER_BYTES,0,D3D12_HEAP_TYPE_UPLOAD,0,&upload));
 COLOR_HR(color_resource(device,0,BUFFER_BYTES,0,D3D12_HEAP_TYPE_READBACK,0,&readback));
 COLOR_HR(ID3D12Resource_Map(upload,0,&range,&mapped));
 {float *v=(float*)((unsigned char*)mapped+VERTEX_OFFSET);v[0]=v[1]=-1;v[2]=v[3]=1;}
 vertex.BufferLocation=ID3D12Resource_GetGPUVirtualAddress(upload)+VERTEX_OFFSET;vertex.SizeInBytes=vertex.StrideInBytes=16;
 input.SemanticName="POSITION";input.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;
 input.InputSlotClass=D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA;input.InstanceDataStepRate=1;
 desc.pRootSignature=config->root;desc.VS.pShaderBytecode=config->vertex;desc.VS.BytecodeLength=config->vertex_bytes;
 desc.InputLayout.NumElements=1;desc.InputLayout.pInputElementDescs=&input;desc.NumRenderTargets=1;
 desc.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
 desc.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;desc.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
 desc.RasterizerState.DepthClipEnable=TRUE;desc.SampleDesc.Count=1;desc.SampleMask=UINT32_MAX;
 desc.BlendState.RenderTarget[0].RenderTargetWriteMask=15;
 for(scale=0;scale<bank->scale_classes;++scale)for(a=0;a<12;++a)for(b=0;b<12;++b)for(converted=0;converted<2;++converted){
  s=classes[a];d=classes[b];
  prepared=xe_transfer_color_d3d12_get(bank,formats[a],formats[b],scale>>1,scale&1,converted);if(!prepared)continue;
  sw=160*(scale&1?1:config->scale_x);sh=16*(scale&1?1:config->scale_y);
  dw=160*(scale&2?1:config->scale_x);dh=16*(scale&2?1:config->scale_y);
  sp=(sw*color_pixel_bytes(s)+255)&~255u;dp=(dw*color_pixel_bytes(d)+255)&~255u;
  if(sp*sh>=REFERENCE_OFFSET || dp*dh>=REFERENCE_OFFSET)goto done;
  for(y=0;y<sh;++y)for(x=0;x<sw*color_pixel_bytes(s)/4;++x){
   p=x+y*sw;((uint32_t*)((unsigned char*)mapped+y*sp))[x]=
    s==3?UINT32_C(0x3c003555):p*UINT32_C(0x9e3779b9)^(p<<13);
  }
  options.mode=1;options.scale_x=config->scale_x;options.scale_y=config->scale_y;
  options.source_format=formats[a];options.destination_format=formats[b];options.source_uint=s>=4;options.destination_uint=d>=4;
  options.source_native=scale&1;options.destination_native=scale>>1;options.source_space=config->source_space;options.value_convert=converted;
  if(!xe_dxil_build_edram_transfer(&options,&binary))goto done;
  desc.PS.pShaderBytecode=binary.data;desc.PS.BytecodeLength=binary.size;desc.RTVFormats[0]=xe_transfer_color_d3d12_view(d);
  COLOR_HR(ID3D12Device_CreateGraphicsPipelineState(device,&desc,&IID_ID3D12PipelineState,(void**)&legacy));
  xe_dxil_shader_binary_free(&binary);
  COLOR_HR(color_resource(device,s,sw,sh,D3D12_HEAP_TYPE_DEFAULT,0,&source));
  COLOR_HR(color_resource(device,d,dw,dh,D3D12_HEAP_TYPE_DEFAULT,1,&dest));
  srv.Format=xe_transfer_color_d3d12_view(s);srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
  srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;
  ID3D12Device_CreateShaderResourceView(device,source,&srv,srv_cpu);ID3D12Device_CreateRenderTargetView(device,dest,NULL,rtv);
  COLOR_HR(ID3D12CommandAllocator_Reset(allocator));COLOR_HR(ID3D12GraphicsCommandList_Reset(list,allocator,NULL));
  image.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;image.pResource=source;
  buffer.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;buffer.pResource=upload;
  buffer.PlacedFootprint.Offset=0;buffer.PlacedFootprint.Footprint.Format=xe_transfer_color_d3d12_view(s);
  buffer.PlacedFootprint.Footprint.Width=sw;buffer.PlacedFootprint.Footprint.Height=sh;buffer.PlacedFootprint.Footprint.Depth=1;buffer.PlacedFootprint.Footprint.RowPitch=sp;
  ID3D12GraphicsCommandList_CopyTextureRegion(list,&image,0,0,0,&buffer,NULL);
  color_transition(list,source,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  ID3D12GraphicsCommandList_SetGraphicsRootSignature(list,config->root);ID3D12GraphicsCommandList_SetDescriptorHeaps(list,1,&srv_heap);
  ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(list,1,srv_gpu);
  words[0]=4|(4u<<10);ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(list,0,4,words,0);
  viewport.Width=(float)dw;viewport.Height=(float)dh;viewport.MaxDepth=1;scissor.right=(LONG)dw;scissor.bottom=(LONG)dh;
  ID3D12GraphicsCommandList_RSSetViewports(list,1,&viewport);ID3D12GraphicsCommandList_RSSetScissorRects(list,1,&scissor);
  ID3D12GraphicsCommandList_OMSetRenderTargets(list,1,&rtv,FALSE,NULL);
  ID3D12GraphicsCommandList_IASetVertexBuffers(list,0,1,&vertex);ID3D12GraphicsCommandList_IASetPrimitiveTopology(list,D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  for(variant=0;variant<2;++variant){
#ifdef XE_COLOR_D3D12_BAD_PACKET
   words[0]=(4|(4u<<10))|(variant?0:1u<<20);
   ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(list,0,4,words,0);
#endif
   ID3D12GraphicsCommandList_SetPipelineState(list,variant?legacy:*prepared);ID3D12GraphicsCommandList_DrawInstanced(list,6,1,0,0);
   color_transition(list,dest,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);
   image.pResource=dest;buffer.pResource=readback;buffer.PlacedFootprint.Offset=variant?REFERENCE_OFFSET:0;
   buffer.PlacedFootprint.Footprint.Format=xe_transfer_color_d3d12_view(d);
   buffer.PlacedFootprint.Footprint.Width=dw;buffer.PlacedFootprint.Footprint.Height=dh;buffer.PlacedFootprint.Footprint.RowPitch=dp;
   ID3D12GraphicsCommandList_CopyTextureRegion(list,&buffer,0,0,0,&image,NULL);
   if(!variant)color_transition(list,dest,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);
  }
  COLOR_HR(ID3D12GraphicsCommandList_Close(list));submitted=(ID3D12CommandList*)list;
  ID3D12CommandQueue_ExecuteCommandLists(queue,1,&submitted);pending=1;
  COLOR_HR(ID3D12CommandQueue_Signal(queue,fence,count+1));COLOR_HR(ID3D12Fence_SetEventOnCompletion(fence,count+1,event));
  if(WaitForSingleObject(event,30000)!=WAIT_OBJECT_0)goto done;
  pending=0;
  range.Begin=0;range.End=BUFFER_BYTES;COLOR_HR(ID3D12Resource_Map(readback,0,&range,&samples));
  for(y=0;y<dh;++y)if(memcmp((unsigned char*)samples+y*dp,(unsigned char*)samples+REFERENCE_OFFSET+y*dp,dw*color_pixel_bytes(d))){
   fprintf(stderr,"image mismatch s%u d%u scale%u converted%u row%u\n",formats[a],formats[b],scale,converted,y);goto done;}
  ID3D12Resource_Unmap(readback,0,NULL);samples=NULL;ID3D12Resource_Release(source);source=NULL;
  ID3D12Resource_Release(dest);dest=NULL;ID3D12PipelineState_Release(legacy);legacy=NULL;++count;
 }
 printf("PASS: %u native prepared/legacy PSO image pairs, single-sample actual typed views\n",count);result=1;
done:
 if(pending){fprintf(stderr,"Pending GPU resources retained on test failure\n");ExitProcess(3);}
 if(samples)ID3D12Resource_Unmap(readback,0,NULL);if(mapped)ID3D12Resource_Unmap(upload,0,NULL);
 xe_dxil_shader_binary_free(&binary);if(legacy)ID3D12PipelineState_Release(legacy);
 if(source)ID3D12Resource_Release(source);if(dest)ID3D12Resource_Release(dest);
 if(upload)ID3D12Resource_Release(upload);if(readback)ID3D12Resource_Release(readback);
 if(srv_heap)ID3D12DescriptorHeap_Release(srv_heap);if(rtv_heap)ID3D12DescriptorHeap_Release(rtv_heap);
 if(fence)ID3D12Fence_Release(fence);if(event)CloseHandle(event);
 if(list)ID3D12GraphicsCommandList_Release(list);if(allocator)ID3D12CommandAllocator_Release(allocator);
 if(queue)ID3D12CommandQueue_Release(queue);return result;
#undef COLOR_HR
}
#endif
