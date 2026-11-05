/* C89 GPU differential: scratch downscale versus final borrowed raw UAV. */
#define COBJMACROS
#define _CRT_SECURE_NO_WARNINGS
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4201)
#endif
#include <windows.h>
#include <initguid.h>
#ifdef __MINGW32__
#include <directx/d3d12.h>
#else
#include <d3d12.h>
#endif
#include <dxgi1_4.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libretro/core_gpu_downscale.h"
#define CHECK(x) do { HRESULT hr_=(x); if (FAILED(hr_)) { fprintf(stderr,"D3D12 %08lx at line %u\n",(unsigned long)hr_,(unsigned)__LINE__); goto done; } } while (0)
static HRESULT buffer(ID3D12Device *device, D3D12_HEAP_TYPE type,
      D3D12_RESOURCE_STATES state, size_t bytes, ID3D12Resource **resource)
{
   D3D12_HEAP_PROPERTIES heap;
   D3D12_RESOURCE_DESC desc;
   memset(&heap,0,sizeof(heap)); heap.Type=type;
   heap.CreationNodeMask=heap.VisibleNodeMask=1;
   memset(&desc,0,sizeof(desc)); desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
   desc.Width=bytes; desc.Height=1; desc.DepthOrArraySize=desc.MipLevels=1;
   desc.SampleDesc.Count=1; desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
   if (type==D3D12_HEAP_TYPE_DEFAULT) desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
   return ID3D12Device_CreateCommittedResource(device,&heap,D3D12_HEAP_FLAG_NONE,
      &desc,state,NULL,&IID_ID3D12Resource,(void **)resource);
}
static void transition(ID3D12GraphicsCommandList *list, ID3D12Resource *resource,
      D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
   D3D12_RESOURCE_BARRIER b;
   memset(&b,0,sizeof(b)); b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
   b.Transition.pResource=resource; b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
   b.Transition.StateBefore=before; b.Transition.StateAfter=after;
   ID3D12GraphicsCommandList_ResourceBarrier(list,1,&b);
}
int main(int argc, char **argv)
{
   IDXGIFactory4 *factory=NULL;
   IDXGIAdapter *adapter=NULL;
   ID3D12Device *device=NULL;
   ID3D12RootSignature *root=NULL;
   ID3D12PipelineState *pipeline=NULL;
   ID3DBlob *serialized=NULL, *errors=NULL;
   ID3D12Resource *input=NULL, *output[2]={NULL,NULL}, *readback=NULL;
   ID3D12DescriptorHeap *heap=NULL;
   ID3D12CommandAllocator *allocator=NULL;
   ID3D12GraphicsCommandList *list=NULL;
   ID3D12CommandQueue *queue=NULL;
   ID3D12Fence *fence=NULL;
   HANDLE event=NULL;
   D3D12_ROOT_PARAMETER parameters[3];
   D3D12_DESCRIPTOR_RANGE ranges[2];
   D3D12_ROOT_SIGNATURE_DESC root_desc;
   D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_desc;
   D3D12_DESCRIPTOR_HEAP_DESC heap_desc;
   D3D12_COMMAND_QUEUE_DESC queue_desc;
   D3D12_CPU_DESCRIPTOR_HANDLE cpu_start, cpu;
   D3D12_GPU_DESCRIPTOR_HANDLE gpu_start, gpu;
   D3D12_SHADER_RESOURCE_VIEW_DESC srv;
   D3D12_UNORDERED_ACCESS_VIEW_DESC uav;
   D3D12_RANGE map_range;
   ID3D12CommandList *submit;
   xe_gpu_downscale_view view = {0};
   unsigned char *mapped=NULL, *shader=NULL;
   FILE *file=NULL;
   long shader_size;
   const size_t source_size=2u*1024u*1024u, output_size=65536;
   const uint32_t offsets[4]={0,64,128,256};
   uint32_t pixel,sx,sy,half,offset_case,tile_bytes,bytes,area,constants[7];
   uint32_t remaining,chunk,stride,cases=0,seed=1,i;
   uint64_t source_offset,destination;
   size_t byte;
   UINT64 serial=0;
   int result=1;
   if (argc!=2) { fprintf(stderr,"usage: %s resolve_downscale_cs.dxil\n",argv[0]); return 2; }
   file=fopen(argv[1],"rb");
   if (!file || fseek(file,0,SEEK_END) || (shader_size=ftell(file))<=0 || shader_size>1048576 || fseek(file,0,SEEK_SET)) goto done;
   shader=(unsigned char *)malloc((size_t)shader_size);
   if (!shader || fread(shader,1,(size_t)shader_size,file)!=(size_t)shader_size) goto done;
   fclose(file); file=NULL;
   CHECK(CreateDXGIFactory1(&IID_IDXGIFactory4,(void **)&factory));
   CHECK(IDXGIFactory4_EnumWarpAdapter(factory,&IID_IDXGIAdapter,(void **)&adapter));
   CHECK(D3D12CreateDevice((IUnknown *)adapter,D3D_FEATURE_LEVEL_12_0,&IID_ID3D12Device,(void **)&device));
   memset(parameters,0,sizeof(parameters)); memset(ranges,0,sizeof(ranges));
   memset(&root_desc,0,sizeof(root_desc)); root_desc.NumParameters=3; root_desc.pParameters=parameters;
   parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
   parameters[0].Constants.Num32BitValues=7;
   for (i=0;i<2;++i) {
      ranges[i].RangeType=i ? D3D12_DESCRIPTOR_RANGE_TYPE_UAV : D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
      ranges[i].NumDescriptors=1;
      parameters[i+1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      parameters[i+1].DescriptorTable.NumDescriptorRanges=1;
      parameters[i+1].DescriptorTable.pDescriptorRanges=&ranges[i];
   }
   CHECK(D3D12SerializeRootSignature(&root_desc,D3D_ROOT_SIGNATURE_VERSION_1,&serialized,&errors));
   CHECK(ID3D12Device_CreateRootSignature(device,0,ID3D10Blob_GetBufferPointer(serialized),
      ID3D10Blob_GetBufferSize(serialized),&IID_ID3D12RootSignature,(void **)&root));
   memset(&pipeline_desc,0,sizeof(pipeline_desc)); pipeline_desc.pRootSignature=root;
   pipeline_desc.CS.pShaderBytecode=shader; pipeline_desc.CS.BytecodeLength=(SIZE_T)shader_size;
   CHECK(ID3D12Device_CreateComputePipelineState(device,&pipeline_desc,&IID_ID3D12PipelineState,(void **)&pipeline));
   CHECK(buffer(device,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ,source_size+output_size,&input));
   for (i=0;i<2;++i) CHECK(buffer(device,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST,output_size,&output[i]));
   CHECK(buffer(device,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST,2*output_size,&readback));
   map_range.Begin=map_range.End=0;
   CHECK(ID3D12Resource_Map(input,0,&map_range,(void **)&mapped));
   for (byte=0;byte<source_size/4;++byte) { seed=seed*1664525u+1013904223u; ((uint32_t *)mapped)[byte]=seed; }
   memset(mapped+source_size,0xA5,output_size); ID3D12Resource_Unmap(input,0,NULL); mapped=NULL;
   memset(&heap_desc,0,sizeof(heap_desc)); heap_desc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
   heap_desc.NumDescriptors=6; heap_desc.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
   CHECK(ID3D12Device_CreateDescriptorHeap(device,&heap_desc,&IID_ID3D12DescriptorHeap,(void **)&heap));
   ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap,&cpu_start);
   ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap,&gpu_start);
   stride=ID3D12Device_GetDescriptorHandleIncrementSize(device,D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
   memset(&queue_desc,0,sizeof(queue_desc)); queue_desc.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
   CHECK(ID3D12Device_CreateCommandQueue(device,&queue_desc,&IID_ID3D12CommandQueue,(void **)&queue));
   CHECK(ID3D12Device_CreateCommandAllocator(device,D3D12_COMMAND_LIST_TYPE_DIRECT,&IID_ID3D12CommandAllocator,(void **)&allocator));
   CHECK(ID3D12Device_CreateCommandList(device,0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator,NULL,&IID_ID3D12GraphicsCommandList,(void **)&list));
   CHECK(ID3D12GraphicsCommandList_Close(list));
   CHECK(ID3D12Device_CreateFence(device,0,D3D12_FENCE_FLAG_NONE,&IID_ID3D12Fence,(void **)&fence));
   event=CreateEvent(NULL,FALSE,FALSE,NULL); if (!event) goto done;
   for (pixel=0;pixel<4;++pixel) for (sx=1;sx<=8;++sx) for (sy=1;sy<=8;++sy)
   for (half=0;half<2;++half) for (offset_case=0;offset_case<4;++offset_case) {
      tile_bytes=1024u<<pixel; bytes=tile_bytes*2; area=sx*sy;
      destination=offsets[offset_case]; source_offset=destination*area;
      CHECK(ID3D12CommandAllocator_Reset(allocator));
      CHECK(ID3D12GraphicsCommandList_Reset(list,allocator,pipeline));
      for (i=0;i<2;++i) {
         if (cases) transition(list,output[i],D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
         ID3D12GraphicsCommandList_CopyBufferRegion(list,output[i],0,input,source_size,output_size);
         transition(list,output[i],D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      }
      ID3D12GraphicsCommandList_SetComputeRootSignature(list,root);
      ID3D12GraphicsCommandList_SetDescriptorHeaps(list,1,&heap);
      memset(constants,0,sizeof(constants)); constants[0]=sx; constants[1]=sy;
      constants[2]=pixel; constants[3]=2; constants[4]=(uint32_t)source_offset; constants[5]=half;
      chunk=0; remaining=2;
      do {
         memset(&srv,0,sizeof(srv)); srv.Format=DXGI_FORMAT_R32_TYPELESS;
         srv.ViewDimension=D3D12_SRV_DIMENSION_BUFFER; srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
         srv.Buffer.NumElements=(UINT)(source_size/4); srv.Buffer.Flags=D3D12_BUFFER_SRV_FLAG_RAW;
         memset(&uav,0,sizeof(uav)); uav.Format=DXGI_FORMAT_R32_TYPELESS;
         uav.ViewDimension=D3D12_UAV_DIMENSION_BUFFER; uav.Buffer.NumElements=(UINT)(output_size/4);
         uav.Buffer.Flags=D3D12_BUFFER_UAV_FLAG_RAW;
         if (chunk) {
            if (!xe_gpu_downscale_plan(source_offset,destination,source_size,output_size,tile_bytes,area,
                remaining,16,0xFFFFFFF0u,1,&view) || view.source_bias || view.destination_bias) goto done;
            srv.Buffer.FirstElement=view.source_offset/4; srv.Buffer.NumElements=view.source_size/4;
            uav.Buffer.FirstElement=view.destination_offset/4; uav.Buffer.NumElements=view.destination_size/4;
            constants[3]=view.tiles; constants[4]=0;
         }
         cpu=cpu_start; cpu.ptr+=(SIZE_T)chunk*2*stride;
         gpu=gpu_start; gpu.ptr+=(UINT64)chunk*2*stride;
         ID3D12Device_CreateShaderResourceView(device,input,&srv,cpu);
         ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(list,1,gpu);
         cpu.ptr+=stride; gpu.ptr+=stride;
         ID3D12Device_CreateUnorderedAccessView(device,output[chunk ? 1 : 0],NULL,&uav,cpu);
         ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(list,2,gpu);
         ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(list,0,7,constants,0);
         ID3D12GraphicsCommandList_Dispatch(list,constants[3],1,1);
         if (chunk) { remaining-=view.tiles; source_offset+=(uint64_t)view.tiles*tile_bytes*area;
            destination+=(uint64_t)view.tiles*tile_bytes; }
         ++chunk;
      } while (remaining);
      for (i=0;i<2;++i) {
         transition(list,output[i],D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
         ID3D12GraphicsCommandList_CopyBufferRegion(list,readback,(UINT64)i*output_size,output[i],0,output_size);
      }
      CHECK(ID3D12GraphicsCommandList_Close(list)); submit=(ID3D12CommandList *)list;
      ID3D12CommandQueue_ExecuteCommandLists(queue,1,&submit);
      /* Failed completion must not destroy buffers still used by the GPU. */
      if (FAILED(ID3D12CommandQueue_Signal(queue,fence,++serial)) ||
            FAILED(ID3D12Fence_SetEventOnCompletion(fence,serial,event)) ||
            WaitForSingleObject(event,10000)!=WAIT_OBJECT_0) {
         fprintf(stderr,"GPU completion failed\n"); exit(1);
      }
      map_range.Begin=0; map_range.End=2*output_size;
      CHECK(ID3D12Resource_Map(readback,0,&map_range,(void **)&mapped));
      if (memcmp(mapped,mapped+output_size+offsets[offset_case],bytes)) {
         fprintf(stderr,"Mismatch: pixel=%u scale=%ux%u half=%u offset=%u\n",pixel,sx,sy,half,offsets[offset_case]); goto done; }
      for (byte=0;byte<output_size;++byte) if ((byte<offsets[offset_case] || byte>=offsets[offset_case]+bytes) && mapped[output_size+byte]!=0xA5) {
         fprintf(stderr,"Guard overwritten: %u\n",(unsigned)byte); goto done; }
      map_range.Begin=map_range.End=0; ID3D12Resource_Unmap(readback,0,&map_range); mapped=NULL;
      ++cases;
   }
   printf("PASS: %u D3D12 WARP differential cases, 8/16/32/64bpp, 1-8x axes, half offsets, borrowed raw views, split dispatches and untouched guards\n",cases);
   result=0;
done:
   if (file) fclose(file);
   free(shader);
   if (mapped && readback) ID3D12Resource_Unmap(readback,0,NULL);
   if (event) CloseHandle(event);
   if (fence) ID3D12Fence_Release(fence);
   if (list) ID3D12GraphicsCommandList_Release(list);
   if (allocator) ID3D12CommandAllocator_Release(allocator);
   if (queue) ID3D12CommandQueue_Release(queue);
   if (heap) ID3D12DescriptorHeap_Release(heap);
   if (readback) ID3D12Resource_Release(readback);
   for (i=0;i<2;++i) if (output[i]) ID3D12Resource_Release(output[i]);
   if (input) ID3D12Resource_Release(input);
   if (pipeline) ID3D12PipelineState_Release(pipeline);
   if (root) ID3D12RootSignature_Release(root);
   if (errors) ID3D10Blob_Release(errors);
   if (serialized) ID3D10Blob_Release(serialized);
   if (device) ID3D12Device_Release(device);
   if (adapter) IDXGIAdapter_Release(adapter);
   if (factory) IDXGIFactory4_Release(factory);
   return result;
}
