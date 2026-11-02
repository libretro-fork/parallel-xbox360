/* Execute overlapping rasterizer-ordered buffer updates on WARP. MSVC C89. */
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <initguid.h>
#if defined(__MINGW32__)
#include <directx/d3d12.h>
#else
#include <d3d12.h>
#endif
#include <dxgi1_4.h>
#include <stdio.h>
#include <stdint.h>
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
  if (type == D3D12_HEAP_TYPE_DEFAULT) desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
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
  static const float vertices[12] = {-1,1,0,1, 3,1,0,1, -1,-3,0,1};
  IDXGIFactory4 *factory = NULL;
  IDXGIAdapter *adapter = NULL;
  ID3D12Device *device = NULL;
  ID3D12RootSignature *root = NULL;
  ID3D12PipelineState *pipeline = NULL;
  ID3D12CommandQueue *queue = NULL;
  ID3D12CommandAllocator *allocator = NULL;
  ID3D12GraphicsCommandList *list = NULL;
  ID3D12CommandList *submitted;
  ID3D12Resource *upload = NULL, *counter = NULL, *readback = NULL;
  ID3D12DescriptorHeap *srv_heap = NULL;
  ID3D12Fence *fence = NULL;
  ID3DBlob *serialized = NULL, *errors = NULL;
  D3D12_ROOT_SIGNATURE_DESC root_desc;
  D3D12_ROOT_PARAMETER parameters[2];
  D3D12_DESCRIPTOR_RANGE srv_range;
  D3D12_DESCRIPTOR_HEAP_DESC heap_desc;
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav;
  D3D12_CPU_DESCRIPTOR_HANDLE cpu;
  D3D12_GPU_DESCRIPTOR_HANDLE gpu;
  D3D12_GRAPHICS_PIPELINE_STATE_DESC desc;
  D3D12_INPUT_ELEMENT_DESC input;
  D3D12_COMMAND_QUEUE_DESC queue_desc;
  D3D12_VERTEX_BUFFER_VIEW vertex_view;
  D3D12_VIEWPORT viewport;
  D3D12_RECT scissor;
  D3D12_RANGE range;
  HANDLE event = NULL;
  void *ps = NULL, *vs = NULL, *mapped = NULL;
  uint32_t *values, expected;
  size_t ps_size = 0, vs_size = 0;
  UINT constants[4] = {0,0,0,0};
  unsigned x, layers = 64, width = 8, height = 8;
  HRESULT status = E_FAIL;
  if (argc != 3 && argc != 4) return 2;
  if (argc == 4) layers = (unsigned)strtoul(argv[3], NULL, 10);
  if (!layers || layers > 1024) return 2;
  ps = read_binary(argv[1], &ps_size); vs = read_binary(argv[2], &vs_size);
  if (!ps || !vs) goto done;
#define CHECK(expression) do { status = (expression); if (FAILED(status)) { fprintf(stderr, "Failed at line %d\n", __LINE__); goto done; } } while (0)
  CHECK(CreateDXGIFactory1(&IID_IDXGIFactory4, (void **)&factory));
  CHECK(IDXGIFactory4_EnumWarpAdapter(factory, &IID_IDXGIAdapter, (void **)&adapter));
  CHECK(D3D12CreateDevice((IUnknown *)adapter, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&device));
  memset(&root_desc, 0, sizeof(root_desc)); memset(parameters, 0, sizeof(parameters)); memset(&srv_range, 0, sizeof(srv_range));
  srv_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV; srv_range.NumDescriptors = 1; srv_range.BaseShaderRegister = 1;
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[0].DescriptorTable.NumDescriptorRanges = 1; parameters[0].DescriptorTable.pDescriptorRanges = &srv_range;
  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[1].Constants.ShaderRegister = 0; parameters[1].Constants.RegisterSpace = 1; parameters[1].Constants.Num32BitValues = 4;
  root_desc.NumParameters = 2; root_desc.pParameters = parameters;
  root_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
  CHECK(D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors));
  CHECK(ID3D12Device_CreateRootSignature(device, 0, ID3D10Blob_GetBufferPointer(serialized),
    ID3D10Blob_GetBufferSize(serialized), &IID_ID3D12RootSignature, (void **)&root));
  memset(&desc, 0, sizeof(desc)); memset(&input, 0, sizeof(input));
  desc.pRootSignature = root; desc.VS.pShaderBytecode = vs; desc.VS.BytecodeLength = vs_size;
  desc.PS.pShaderBytecode = ps; desc.PS.BytecodeLength = ps_size;
  input.SemanticName = "POSITION"; input.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
  desc.InputLayout.pInputElementDescs = &input; desc.InputLayout.NumElements = 1;
  desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID; desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  desc.SampleMask = UINT_MAX; desc.SampleDesc.Count = 1;
  CHECK(ID3D12Device_CreateGraphicsPipelineState(device, &desc, &IID_ID3D12PipelineState, (void **)&pipeline));
  CHECK(buffer(device, D3D12_HEAP_TYPE_UPLOAD, 123392, D3D12_RESOURCE_STATE_GENERIC_READ, &upload));
  CHECK(buffer(device, D3D12_HEAP_TYPE_READBACK, 123136, D3D12_RESOURCE_STATE_COPY_DEST, &readback));
  CHECK(buffer(device, D3D12_HEAP_TYPE_DEFAULT, 256, D3D12_RESOURCE_STATE_COPY_DEST, &counter));
  memset(&range, 0, sizeof(range)); CHECK(ID3D12Resource_Map(upload, 0, &range, &mapped));
  memset(mapped, 0, 123392);
  memcpy((unsigned char *)mapped + 122880, vertices, sizeof(vertices));
  ID3D12Resource_Unmap(upload, 0, NULL); mapped = NULL;
  memset(&heap_desc, 0, sizeof(heap_desc)); heap_desc.NumDescriptors = 1;
  heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  CHECK(ID3D12Device_CreateDescriptorHeap(device, &heap_desc, &IID_ID3D12DescriptorHeap, (void **)&srv_heap));
  ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(srv_heap, &cpu);
  memset(&uav, 0, sizeof(uav)); uav.Format = DXGI_FORMAT_R32_TYPELESS;
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER; uav.Buffer.NumElements = 64;
  uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
  ID3D12Device_CreateUnorderedAccessView(device, counter, NULL, &uav, cpu);
  memset(&queue_desc, 0, sizeof(queue_desc));
  CHECK(ID3D12Device_CreateCommandQueue(device, &queue_desc, &IID_ID3D12CommandQueue, (void **)&queue));
  CHECK(ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void **)&allocator));
  CHECK(ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, pipeline, &IID_ID3D12GraphicsCommandList, (void **)&list));
  ID3D12GraphicsCommandList_CopyBufferRegion(list, counter, 0, upload, 123136, 256);
  transition(list, counter, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  ID3D12GraphicsCommandList_SetGraphicsRootSignature(list, root);
  ID3D12GraphicsCommandList_SetDescriptorHeaps(list, 1, &srv_heap);
  ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(srv_heap, &gpu);
  ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(list, 0, gpu);
  ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(list, 1, 4, constants, 0);
  memset(&viewport, 0, sizeof(viewport)); viewport.Width = (float)width; viewport.Height = (float)height; viewport.MaxDepth = 1;
  scissor.left = 0; scissor.top = 0; scissor.right = (LONG)width; scissor.bottom = (LONG)height;
  ID3D12GraphicsCommandList_RSSetViewports(list, 1, &viewport); ID3D12GraphicsCommandList_RSSetScissorRects(list, 1, &scissor);
  ID3D12GraphicsCommandList_OMSetRenderTargets(list, 0, NULL, FALSE, NULL);
  vertex_view.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(upload) + 122880;
  vertex_view.SizeInBytes = sizeof(vertices); vertex_view.StrideInBytes = 16;
  ID3D12GraphicsCommandList_IASetVertexBuffers(list, 0, 1, &vertex_view);
  ID3D12GraphicsCommandList_IASetPrimitiveTopology(list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  ID3D12GraphicsCommandList_DrawInstanced(list, 3, layers, 0, 0);
  transition(list, counter, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
  ID3D12GraphicsCommandList_CopyBufferRegion(list, readback, 122880, counter, 0, 256);
  CHECK(ID3D12GraphicsCommandList_Close(list));
  submitted = (ID3D12CommandList *)list; ID3D12CommandQueue_ExecuteCommandLists(queue, 1, &submitted);
  CHECK(ID3D12Device_CreateFence(device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&fence));
  event = CreateEventA(NULL, FALSE, FALSE, NULL); if (!event) { status = E_FAIL; goto done; }
  CHECK(ID3D12CommandQueue_Signal(queue, fence, 1)); CHECK(ID3D12Fence_SetEventOnCompletion(fence, 1, event));
  if (WaitForSingleObject(event, 10000) != WAIT_OBJECT_0) { status = E_FAIL; goto done; }
  range.Begin = 0; range.End = 123136; CHECK(ID3D12Resource_Map(readback, 0, &range, &mapped)); values = (uint32_t *)mapped;
  values += 122880/4;
  expected = 0;
  for (x = 0; x < layers; ++x) expected = expected*3u+1u;
  for (x = 0; x < width*height; ++x) if (values[x] != expected) {
    fprintf(stderr, "Ordered pixel %u mismatch: %08lX != %08lX\n", x, (unsigned long)values[x], (unsigned long)expected); status = E_FAIL; goto done;
  }
  printf("Native ROV: %u ordered updates matched at all %u pixels.\n", layers, width*height);
done:
  if (mapped && readback) ID3D12Resource_Unmap(readback, 0, NULL);
  printf("WARP ROV: 0x%08lX\n", (unsigned long)status);
  if (event) CloseHandle(event);
  if (fence) ID3D12Fence_Release(fence);
  if (list) ID3D12GraphicsCommandList_Release(list);
  if (allocator) ID3D12CommandAllocator_Release(allocator);
  if (queue) ID3D12CommandQueue_Release(queue);
  if (pipeline) ID3D12PipelineState_Release(pipeline);
  if (srv_heap) ID3D12DescriptorHeap_Release(srv_heap);
  if (readback) ID3D12Resource_Release(readback);
  if (counter) ID3D12Resource_Release(counter);
  if (upload) ID3D12Resource_Release(upload);
  if (root) ID3D12RootSignature_Release(root);
  if (errors) ID3D10Blob_Release(errors);
  if (serialized) ID3D10Blob_Release(serialized);
  if (device) ID3D12Device_Release(device);
  if (adapter) IDXGIAdapter_Release(adapter);
  if (factory) IDXGIFactory4_Release(factory);
  free(vs); free(ps);
  return FAILED(status) ? 1 : 0;
}
