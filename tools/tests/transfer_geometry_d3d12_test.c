/* Exact instanced transfer vertex output through WARP. MSVC C89. */
#include "libretro/core_transfer_geometry.h"
enum { RECTANGLES=257, OUTPUT_BYTES=RECTANGLES*6*16+256 };
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
int main(int argc, char **argv) {
  static float rectangles[RECTANGLES][4];
  static const unsigned corners[6][2]={{0,1},{2,1},{0,3},{0,3},{2,1},{2,3}};
  IDXGIFactory4 *factory = NULL;
  IDXGIAdapter *adapter = NULL;
  ID3D12Device *device = NULL;
  ID3D12RootSignature *root = NULL;
  ID3D12PipelineState *pipeline = NULL;
  ID3D12CommandQueue *queue = NULL;
  ID3D12CommandAllocator *allocator = NULL;
  ID3D12GraphicsCommandList *list = NULL;
  ID3D12CommandList *submitted;
  ID3D12Resource *upload = NULL, *output = NULL, *readback = NULL;
  ID3D12Fence *fence = NULL;
  ID3DBlob *serialized = NULL, *errors = NULL;
  D3D12_ROOT_SIGNATURE_DESC root_desc;
  D3D12_GRAPHICS_PIPELINE_STATE_DESC desc;
  D3D12_INPUT_ELEMENT_DESC input[1];
  D3D12_SO_DECLARATION_ENTRY declarations[1];
  D3D12_COMMAND_QUEUE_DESC queue_desc;
  D3D12_VERTEX_BUFFER_VIEW vertex_view;
  D3D12_STREAM_OUTPUT_BUFFER_VIEW output_view;
  D3D12_RANGE range;
  HANDLE event = NULL;
  void *vs = NULL, *mapped = NULL;
  float expected[4], *values;
  size_t vs_size=0;
  UINT stride=16;
  UINT64 written;
  unsigned i,j;
  HRESULT status=E_FAIL;
  if(argc!=2) return 2;
  vs=read_binary(argv[1],&vs_size); if(!vs) goto done;
  for(i=0;i<RECTANGLES;++i) {
    rectangles[i][0]=-1.0f+(float)i/512.0f;
    rectangles[i][1]=1.0f-(float)i/512.0f;
    rectangles[i][2]=rectangles[i][0]+.125f;
    rectangles[i][3]=rectangles[i][1]-.25f;
  }
  status = CreateDXGIFactory1(&IID_IDXGIFactory4, (void **)&factory);
  if (FAILED(status)) goto done;
  status = IDXGIFactory4_EnumWarpAdapter(factory, &IID_IDXGIAdapter, (void **)&adapter);
  if (FAILED(status)) goto done;
  status = D3D12CreateDevice((IUnknown *)adapter, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&device);
  if (FAILED(status)) goto done;
  memset(&root_desc,0,sizeof(root_desc));
  root_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT | D3D12_ROOT_SIGNATURE_FLAG_ALLOW_STREAM_OUTPUT;
  status = D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors);
  if (FAILED(status)) goto done;
  status = ID3D12Device_CreateRootSignature(device, 0, ID3D10Blob_GetBufferPointer(serialized),
    ID3D10Blob_GetBufferSize(serialized), &IID_ID3D12RootSignature, (void **)&root);
  if (FAILED(status)) goto done;
  memset(&desc, 0, sizeof(desc)); memset(input, 0, sizeof(input)); memset(declarations, 0, sizeof(declarations));
  desc.pRootSignature = root; desc.VS.pShaderBytecode = vs; desc.VS.BytecodeLength = vs_size;
  input[0].SemanticName="POSITION"; input[0].Format=DXGI_FORMAT_R32G32B32A32_FLOAT;
  input[0].InputSlotClass=D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA;
  input[0].InstanceDataStepRate=1;
  desc.InputLayout.pInputElementDescs=input; desc.InputLayout.NumElements=1;
  declarations[0].SemanticName="SV_Position"; declarations[0].ComponentCount=4;
  desc.StreamOutput.pSODeclaration=declarations; desc.StreamOutput.NumEntries=1;
  desc.StreamOutput.pBufferStrides=&stride; desc.StreamOutput.NumStrides=1;
  desc.StreamOutput.RasterizedStream=D3D12_SO_NO_RASTERIZED_STREAM;
  desc.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID; desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  desc.RasterizerState.DepthClipEnable = TRUE; desc.SampleMask = UINT_MAX; desc.SampleDesc.Count = 1;
  status = ID3D12Device_CreateGraphicsPipelineState(device, &desc, &IID_ID3D12PipelineState, (void **)&pipeline);
  if (FAILED(status)) goto done;
  status = buffer(device, D3D12_HEAP_TYPE_UPLOAD, 65536, D3D12_RESOURCE_STATE_GENERIC_READ, &upload);
  if (FAILED(status)) goto done;
  status = buffer(device, D3D12_HEAP_TYPE_DEFAULT, OUTPUT_BYTES, D3D12_RESOURCE_STATE_COPY_DEST, &output);
  if (FAILED(status)) goto done;
  status = buffer(device, D3D12_HEAP_TYPE_READBACK, OUTPUT_BYTES, D3D12_RESOURCE_STATE_COPY_DEST, &readback);
  if (FAILED(status)) goto done;
  range.Begin = 0; range.End = 0;
  status = ID3D12Resource_Map(upload, 0, &range, &mapped);
  if (FAILED(status)) goto done;
  memset(mapped,0,65536); memcpy((unsigned char*)mapped+256,rectangles,sizeof(rectangles));
  ID3D12Resource_Unmap(upload, 0, NULL); mapped = NULL;
  memset(&queue_desc, 0, sizeof(queue_desc));
  status = ID3D12Device_CreateCommandQueue(device, &queue_desc, &IID_ID3D12CommandQueue, (void **)&queue);
  if (FAILED(status)) goto done;
  status = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void **)&allocator);
  if (FAILED(status)) goto done;
  status = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, pipeline,
    &IID_ID3D12GraphicsCommandList, (void **)&list);
  if (FAILED(status)) goto done;
  ID3D12GraphicsCommandList_CopyBufferRegion(list, output, 0, upload, 0, 8);
  transition(list, output, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_STREAM_OUT);
  vertex_view.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(upload)+256;
  vertex_view.SizeInBytes = sizeof(rectangles); vertex_view.StrideInBytes = 16;
  output_view.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(output) + 256;
  output_view.SizeInBytes = OUTPUT_BYTES-256; output_view.BufferFilledSizeLocation = ID3D12Resource_GetGPUVirtualAddress(output);
  ID3D12GraphicsCommandList_SetGraphicsRootSignature(list, root);
  ID3D12GraphicsCommandList_IASetVertexBuffers(list, 0, 1, &vertex_view);
  ID3D12GraphicsCommandList_IASetPrimitiveTopology(list,D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  ID3D12GraphicsCommandList_SOSetTargets(list, 0, 1, &output_view);
  ID3D12GraphicsCommandList_DrawInstanced(list,6,RECTANGLES,0,0);
  transition(list, output, D3D12_RESOURCE_STATE_STREAM_OUT, D3D12_RESOURCE_STATE_COPY_SOURCE);
  ID3D12GraphicsCommandList_CopyResource(list, readback, output);
  status = ID3D12GraphicsCommandList_Close(list);
  if (FAILED(status)) goto done;
  submitted = (ID3D12CommandList *)list; ID3D12CommandQueue_ExecuteCommandLists(queue, 1, &submitted);
  status = ID3D12Device_CreateFence(device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&fence);
  if (FAILED(status)) goto done;
  event = CreateEventA(NULL, FALSE, FALSE, NULL);
  if (!event) { status = E_FAIL; goto done; }
  status = ID3D12CommandQueue_Signal(queue, fence, 1);
  if (FAILED(status)) goto done;
  status = ID3D12Fence_SetEventOnCompletion(fence, 1, event);
  if (FAILED(status)) goto done;
  if (WaitForSingleObject(event, 10000) != WAIT_OBJECT_0) { status = E_FAIL; goto done; }
  range.Begin = 0; range.End = OUTPUT_BYTES;
  status = ID3D12Resource_Map(readback, 0, &range, &mapped);
  if (FAILED(status)) goto done;
  memcpy(&written, mapped, sizeof(written));
  if(written!=RECTANGLES*6*16) {status=E_FAIL;goto done;}
  values=(float*)((unsigned char*)mapped+256);
  for(i=0;i<RECTANGLES;++i) for(j=0;j<6;++j) {
    expected[0]=rectangles[i][corners[j][0]];
    expected[1]=rectangles[i][corners[j][1]];
    expected[2]=0; expected[3]=1;
    if(memcmp(values+(i*6+j)*4,expected,16)) {
      fprintf(stderr,"Transfer vertex mismatch at instance %u vertex %u\n",i,j);
      status=E_FAIL;goto done;
    }
  }
  puts("PASS: 257 instanced rectangles, 1542 exact ordered transfer vertices");

done:
  if (mapped && readback) ID3D12Resource_Unmap(readback, 0, NULL);
  printf("D3D12 WARP transfer: 0x%08lX\n", (unsigned long)status);
  if (event) CloseHandle(event);
  if (fence) ID3D12Fence_Release(fence);
  if (list) ID3D12GraphicsCommandList_Release(list);
  if (allocator) ID3D12CommandAllocator_Release(allocator);
  if (queue) ID3D12CommandQueue_Release(queue);
  if (pipeline) ID3D12PipelineState_Release(pipeline);
  if (readback) ID3D12Resource_Release(readback);
  if (output) ID3D12Resource_Release(output);
  if (upload) ID3D12Resource_Release(upload);
  if (root) ID3D12RootSignature_Release(root);
  if (errors) ID3D10Blob_Release(errors);
  if (serialized) ID3D10Blob_Release(serialized);
  if (device) ID3D12Device_Release(device);
  if (adapter) IDXGIAdapter_Release(adapter);
  if (factory) IDXGIFactory4_Release(factory);
  free(vs);
  return FAILED(status) ? 1 : 0;
}
