/* One native compute PSO, changing runtime sample layouts. BSD; C89. */

#ifdef XE_TRANSFER_UINT_COLOR_TEST
#include "transfer_uint_color_cases.h"
#else
#include "transfer_sample_cases.h"
#endif
enum { INPUT_BYTES=XE_SAMPLE_CASES*128, OUTPUT_BYTES=XE_SAMPLE_OUTPUT_CASES*64 };
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <initguid.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *read_binary(const char *path, size_t *size) {
  FILE *file = fopen(path, "rb");
  void *data;
  long length;
  if (!file) return NULL;
  if (fseek(file, 0, SEEK_END) || (length = ftell(file)) <= 0 || fseek(file, 0, SEEK_SET)) { fclose(file); return NULL; }
  data = malloc((size_t)length);
  if (!data || fread(data, 1, (size_t)length, file) != (size_t)length) { free(data); fclose(file); return NULL; }
  fclose(file); *size = (size_t)length;
  return data;
}
static HRESULT buffer(ID3D12Device *device, D3D12_HEAP_TYPE type, UINT64 size,
                         D3D12_RESOURCE_STATES state, ID3D12Resource **resource) {
  D3D12_HEAP_PROPERTIES heap;
  D3D12_RESOURCE_DESC desc;
  memset(&heap, 0, sizeof(heap)); heap.Type = type;
  memset(&desc, 0, sizeof(desc)); desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = size; desc.Height = 1; desc.DepthOrArraySize = 1; desc.MipLevels = 1;
  desc.SampleDesc.Count = 1; desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  if(type==D3D12_HEAP_TYPE_DEFAULT)desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  return ID3D12Device_CreateCommittedResource(device, &heap, D3D12_HEAP_FLAG_NONE,
    &desc, state, NULL, &IID_ID3D12Resource, (void **)resource);
}
static void transition(ID3D12GraphicsCommandList *list, ID3D12Resource *resource,
                          D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
  D3D12_RESOURCE_BARRIER barrier;
  memset(&barrier, 0, sizeof(barrier)); barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = resource; barrier.Transition.StateBefore = before;
  barrier.Transition.StateAfter = after; barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &barrier);
}
int main(int argc,char **argv) {
 IDXGIFactory4 *factory=NULL;IDXGIAdapter *adapter=NULL;ID3D12Device *device=NULL;
 ID3D12RootSignature *root=NULL;ID3D12PipelineState *pipeline=NULL;
 ID3D12CommandQueue *queue=NULL;ID3D12CommandAllocator *allocator=NULL;
 ID3D12GraphicsCommandList *list=NULL;ID3D12CommandList *submitted;
 ID3D12Resource *input=NULL,*initial=NULL,*output=NULL,*readback=NULL;
 ID3D12Fence *fence=NULL;ID3DBlob *serialized=NULL,*errors=NULL;
 D3D12_ROOT_SIGNATURE_DESC root_desc;D3D12_ROOT_PARAMETER params[3];
 D3D12_COMPUTE_PIPELINE_STATE_DESC desc;D3D12_COMMAND_QUEUE_DESC queue_desc;
 D3D12_RANGE range;HANDLE event=NULL;void *code=NULL;size_t code_size=0;
 uint32_t *input_data=NULL,*initial_data=NULL,*readback_data=NULL;
 uint32_t limits[4],wanted[7];unsigned round,i,lane,cases=0;
 HRESULT status=E_FAIL;
 if(argc!=2)return 1;
 code=read_binary(argv[1],&code_size);if(!code)goto done;
 status=CreateDXGIFactory1(&IID_IDXGIFactory4,(void**)&factory);if(FAILED(status))goto done;
 status=IDXGIFactory4_EnumWarpAdapter(factory,&IID_IDXGIAdapter,(void**)&adapter);if(FAILED(status))goto done;
 status=D3D12CreateDevice((IUnknown*)adapter,D3D_FEATURE_LEVEL_11_0,&IID_ID3D12Device,(void**)&device);if(FAILED(status))goto done;
 memset(&root_desc,0,sizeof(root_desc));memset(params,0,sizeof(params));
 params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;
 params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;
 params[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
 params[2].Constants.Num32BitValues=4;
 root_desc.NumParameters=3;root_desc.pParameters=params;
 status=D3D12SerializeRootSignature(&root_desc,D3D_ROOT_SIGNATURE_VERSION_1,&serialized,&errors);if(FAILED(status))goto done;
 status=ID3D12Device_CreateRootSignature(device,0,ID3D10Blob_GetBufferPointer(serialized),ID3D10Blob_GetBufferSize(serialized),&IID_ID3D12RootSignature,(void**)&root);if(FAILED(status))goto done;
 memset(&desc,0,sizeof(desc));desc.pRootSignature=root;desc.CS.pShaderBytecode=code;desc.CS.BytecodeLength=code_size;
 status=ID3D12Device_CreateComputePipelineState(device,&desc,&IID_ID3D12PipelineState,(void**)&pipeline);if(FAILED(status))goto done;
 status=buffer(device,D3D12_HEAP_TYPE_UPLOAD,INPUT_BYTES,D3D12_RESOURCE_STATE_GENERIC_READ,&input);if(FAILED(status))goto done;
 status=buffer(device,D3D12_HEAP_TYPE_UPLOAD,OUTPUT_BYTES,D3D12_RESOURCE_STATE_GENERIC_READ,&initial);if(FAILED(status))goto done;
 status=buffer(device,D3D12_HEAP_TYPE_DEFAULT,OUTPUT_BYTES,D3D12_RESOURCE_STATE_COPY_DEST,&output);if(FAILED(status))goto done;
 status=buffer(device,D3D12_HEAP_TYPE_READBACK,OUTPUT_BYTES,D3D12_RESOURCE_STATE_COPY_DEST,&readback);if(FAILED(status))goto done;
 range.Begin=range.End=0;
 status=ID3D12Resource_Map(input,0,&range,(void**)&input_data);if(FAILED(status))goto done;
 status=ID3D12Resource_Map(initial,0,&range,(void**)&initial_data);if(FAILED(status))goto done;
 memset(initial_data,0x6d,OUTPUT_BYTES);
 memset(&queue_desc,0,sizeof(queue_desc));
 status=ID3D12Device_CreateCommandQueue(device,&queue_desc,&IID_ID3D12CommandQueue,(void**)&queue);if(FAILED(status))goto done;
 status=ID3D12Device_CreateCommandAllocator(device,D3D12_COMMAND_LIST_TYPE_DIRECT,&IID_ID3D12CommandAllocator,(void**)&allocator);if(FAILED(status))goto done;
 status=ID3D12Device_CreateCommandList(device,0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator,pipeline,&IID_ID3D12GraphicsCommandList,(void**)&list);if(FAILED(status))goto done;
 status=ID3D12Device_CreateFence(device,0,D3D12_FENCE_FLAG_NONE,&IID_ID3D12Fence,(void**)&fence);if(FAILED(status))goto done;
 event=CreateEventA(NULL,FALSE,FALSE,NULL);if(!event){status=E_FAIL;goto done;}
 for(round=0;round<3;++round) {
  limits[0]=round==1?XE_SAMPLE_CASES-17:XE_SAMPLE_CASES;limits[1]=XE_SAMPLE_OUTPUT_CASES;limits[2]=limits[3]=0;
  for(i=0;i<XE_SAMPLE_CASES;++i)xe_sample_input(input_data+i*32,i,round);
  if(round) {
   status=ID3D12CommandAllocator_Reset(allocator);if(FAILED(status))goto done;
   status=ID3D12GraphicsCommandList_Reset(list,allocator,pipeline);if(FAILED(status))goto done;
   transition(list,output,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
  }
  ID3D12GraphicsCommandList_CopyResource(list,output,initial);
  transition(list,output,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  ID3D12GraphicsCommandList_SetComputeRootSignature(list,root);
  ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(list,0,ID3D12Resource_GetGPUVirtualAddress(output));
  ID3D12GraphicsCommandList_SetComputeRootShaderResourceView(list,1,ID3D12Resource_GetGPUVirtualAddress(input));
  ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(list,2,4,limits,0);
  ID3D12GraphicsCommandList_Dispatch(list,(XE_SAMPLE_OUTPUT_CASES+63)/64+1,1,1);
  transition(list,output,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
  ID3D12GraphicsCommandList_CopyResource(list,readback,output);
  status=ID3D12GraphicsCommandList_Close(list);if(FAILED(status))goto done;
  submitted=(ID3D12CommandList*)list;ID3D12CommandQueue_ExecuteCommandLists(queue,1,&submitted);
  status=ID3D12CommandQueue_Signal(queue,fence,round+1);if(FAILED(status))goto done;
  status=ID3D12Fence_SetEventOnCompletion(fence,round+1,event);if(FAILED(status))goto done;
  if(WaitForSingleObject(event,10000)!=WAIT_OBJECT_0){fprintf(stderr,"GPU timeout; retaining pending test resources\n");exit(1);}
  range.Begin=0;range.End=OUTPUT_BYTES;
  status=ID3D12Resource_Map(readback,0,&range,(void**)&readback_data);if(FAILED(status))goto done;
  for(i=0;i<XE_SAMPLE_OUTPUT_CASES;++i) {
   if(i<limits[0])xe_sample_expected(input_data+i*32,wanted);
   for(lane=0;lane<16;++lane) {
    uint32_t expected=i<limits[0] && lane<7?wanted[lane]:0x6d6d6d6du;
    if(readback_data[i*16+lane]!=expected){
     fprintf(stderr,"WARP round%u case%u lane%u: %08x != %08x\n",round,i,lane,readback_data[i*16+lane],expected);status=E_FAIL;goto done;
    }
   }
   if(i<limits[0])++cases;
  }
  range.Begin=range.End=0;ID3D12Resource_Unmap(readback,0,&range);readback_data=NULL;
 }
#ifdef XE_TRANSFER_UINT_COLOR_TEST
 printf("PASS: WARP %u dynamic integer color records, independent payloads, guards, one module and PSO\n",cases);
#else
 printf("PASS: WARP %u dynamic sample records, bounds/padding guards, one module and PSO\n",cases);
#endif
done:
 printf("D3D12 sample status: %08lX\n",(unsigned long)status);
 if(readback_data)ID3D12Resource_Unmap(readback,0,NULL);
 if(input_data)ID3D12Resource_Unmap(input,0,NULL);
 if(initial_data)ID3D12Resource_Unmap(initial,0,NULL);
 if(event)CloseHandle(event);
 if(fence)ID3D12Fence_Release(fence);
 if(list)ID3D12GraphicsCommandList_Release(list);
 if(allocator)ID3D12CommandAllocator_Release(allocator);
 if(queue)ID3D12CommandQueue_Release(queue);
 if(pipeline)ID3D12PipelineState_Release(pipeline);
 if(readback)ID3D12Resource_Release(readback);
 if(output)ID3D12Resource_Release(output);
 if(initial)ID3D12Resource_Release(initial);
 if(input)ID3D12Resource_Release(input);
 if(root)ID3D12RootSignature_Release(root);
 if(errors)ID3D10Blob_Release(errors);
 if(serialized)ID3D10Blob_Release(serialized);
 if(device)ID3D12Device_Release(device);
 if(adapter)IDXGIAdapter_Release(adapter);
 if(factory)IDXGIFactory4_Release(factory);
 free(code);return FAILED(status)?1:0;
}
