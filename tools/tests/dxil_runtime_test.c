/* C89 fixture: ask D3D12 WARP to accept the native DXIL container. */
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

static HRESULT buffer(ID3D12Device *device, D3D12_HEAP_TYPE heap_type,
                       D3D12_RESOURCE_STATES state, size_t bytes,
                       ID3D12Resource **resource)
{
  D3D12_HEAP_PROPERTIES heap;
  D3D12_RESOURCE_DESC desc;
  memset(&heap, 0, sizeof(heap));
  heap.Type = heap_type;
  heap.CreationNodeMask = 1; heap.VisibleNodeMask = 1;
  memset(&desc, 0, sizeof(desc));
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = bytes; desc.Height = 1; desc.DepthOrArraySize = 1;
  desc.MipLevels = 1; desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  if (heap_type == D3D12_HEAP_TYPE_DEFAULT)
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  return ID3D12Device_CreateCommittedResource(device, &heap, D3D12_HEAP_FLAG_NONE,
      &desc, state, NULL, &IID_ID3D12Resource, (void **)resource);
}

static HRESULT execute(ID3D12Device *device, ID3D12RootSignature *root,
                        ID3D12PipelineState *pipeline, const uint32_t *expected,
                        size_t bytes, int golden_records,
                        ID3D12Resource *input, ID3D12Resource *constants)
{
  ID3D12Resource *output = NULL;
  ID3D12Resource *readback = NULL;
  ID3D12CommandQueue *queue = NULL;
  ID3D12CommandAllocator *allocator = NULL;
  ID3D12GraphicsCommandList *list = NULL;
  ID3D12Fence *fence = NULL;
  ID3D12CommandList *commands[1];
  D3D12_COMMAND_QUEUE_DESC queue_desc;
  D3D12_RESOURCE_BARRIER barrier;
  D3D12_RANGE range;
  HANDLE event = NULL;
  HRESULT status;
  uint32_t *values = NULL;
  size_t i;
  int same;
  status = buffer(device, D3D12_HEAP_TYPE_DEFAULT,
                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS, bytes, &output);
  if (FAILED(status)) goto done;
  status = buffer(device, D3D12_HEAP_TYPE_READBACK,
                   D3D12_RESOURCE_STATE_COPY_DEST, bytes, &readback);
  if (FAILED(status)) goto done;
  memset(&queue_desc, 0, sizeof(queue_desc));
  status = ID3D12Device_CreateCommandQueue(device, &queue_desc,
      &IID_ID3D12CommandQueue, (void **)&queue);
  if (FAILED(status)) goto done;
  status = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
      &IID_ID3D12CommandAllocator, (void **)&allocator);
  if (FAILED(status)) goto done;
  status = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
      allocator, pipeline, &IID_ID3D12GraphicsCommandList, (void **)&list);
  if (FAILED(status)) goto done;
  ID3D12GraphicsCommandList_SetComputeRootSignature(list, root);
  ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(list, 0,
      ID3D12Resource_GetGPUVirtualAddress(output));
  if (input && constants) {
    ID3D12GraphicsCommandList_SetComputeRootShaderResourceView(list, 1,
        ID3D12Resource_GetGPUVirtualAddress(input));
    ID3D12GraphicsCommandList_SetComputeRootConstantBufferView(list, 2,
        ID3D12Resource_GetGPUVirtualAddress(constants));
  }
  ID3D12GraphicsCommandList_Dispatch(list, golden_records ? 1 : 32, 1, 1);
  memset(&barrier, 0, sizeof(barrier));
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = output;
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &barrier);
  ID3D12GraphicsCommandList_CopyResource(list, readback, output);
  status = ID3D12GraphicsCommandList_Close(list);
  if (FAILED(status)) goto done;
  status = ID3D12Device_CreateFence(device, 0, D3D12_FENCE_FLAG_NONE,
      &IID_ID3D12Fence, (void **)&fence);
  if (FAILED(status)) goto done;
  event = CreateEventW(NULL, FALSE, FALSE, NULL);
  if (!event) { status = E_FAIL; goto done; }
  commands[0] = (ID3D12CommandList *)list;
  ID3D12CommandQueue_ExecuteCommandLists(queue, 1, commands);
  status = ID3D12CommandQueue_Signal(queue, fence, 1);
  if (FAILED(status)) goto done;
  status = ID3D12Fence_SetEventOnCompletion(fence, 1, event);
  if (FAILED(status)) goto done;
  if (WaitForSingleObject(event, 10000) != WAIT_OBJECT_0) { status = E_FAIL; goto done; }
  range.Begin = 0; range.End = bytes;
  status = ID3D12Resource_Map(readback, 0, &range, (void **)&values);
  if (FAILED(status)) goto done;
  for (i = 0; i < bytes / sizeof(uint32_t); ++i) {
    same = values[i] == expected[i];
    if ((!golden_records || i % 8 < 4) &&
        (expected[i] & UINT32_C(0x7f800000)) == UINT32_C(0x7f800000) &&
        (expected[i] & UINT32_C(0x007fffff)))
      same = (values[i] & UINT32_C(0x7f800000)) == UINT32_C(0x7f800000) &&
        (values[i] & UINT32_C(0x007fffff));
    if (!same) {
      fprintf(stderr, "word %lu: got %08lX, expected %08lX\n", (unsigned long)i,
        (unsigned long)values[i], (unsigned long)expected[i]);
      status = E_FAIL; break;
    }
  }
  range.Begin = 0; range.End = 0;
  ID3D12Resource_Unmap(readback, 0, &range);
  if (SUCCEEDED(status)) printf("Native DXIL execution: %lu words matched.\n",
    (unsigned long)(bytes / sizeof(uint32_t)));
done:
  if (event) CloseHandle(event);
  if (fence) ID3D12Fence_Release(fence);
  if (list) ID3D12GraphicsCommandList_Release(list);
  if (allocator) ID3D12CommandAllocator_Release(allocator);
  if (queue) ID3D12CommandQueue_Release(queue);
  if (readback) ID3D12Resource_Release(readback);
  if (output) ID3D12Resource_Release(output);
  return status;
}

/* Read fixture bytes directly into mapped upload storage. */
static HRESULT upload_file(ID3D12Device *device, const char *path, ID3D12Resource **resource) {
  FILE *file;
  long size;
  size_t aligned;
  unsigned char *mapped;
  D3D12_RANGE range;
  HRESULT status;
  file = fopen(path, "rb");
  if (!file) return E_FAIL;
  if (fseek(file, 0, SEEK_END) || (size = ftell(file)) <= 0 ||
      (size_t)size > (size_t)-1 - 255 || fseek(file, 0, SEEK_SET)) {
    fclose(file); return E_FAIL;
  }
  aligned = ((size_t)size + 255) & ~(size_t)255;
  status = buffer(device, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ,
    aligned, resource);
  if (FAILED(status)) { fclose(file); return status; }
  range.Begin = 0; range.End = 0;
  status = ID3D12Resource_Map(*resource, 0, &range, (void **)&mapped);
  if (FAILED(status)) { fclose(file); return status; }
  if (fread(mapped, 1, (size_t)size, file) != (size_t)size) status = E_FAIL;
  memset(mapped + size, 0, aligned - (size_t)size);
  if (fclose(file)) status = E_FAIL;
  range.End = aligned;
  ID3D12Resource_Unmap(*resource, 0, &range);
  return status;
}

static uint32_t *read_expected(const char *path, size_t *bytes)
{
  FILE *file;
  long size;
  uint32_t *data;
  file = fopen(path, "rb");
  if (!file) return NULL;
  if (fseek(file, 0, SEEK_END) || (size = ftell(file)) <= 0 ||
      size % 32 || fseek(file, 0, SEEK_SET)) { fclose(file); return NULL; }
  data = (uint32_t *)malloc((size_t)size);
  if (!data) { fclose(file); return NULL; }
  if (fread(data, 1, (size_t)size, file) != (size_t)size) {
    fclose(file); free(data); return NULL;
  }
  if (fclose(file)) { free(data); return NULL; }
  *bytes = (size_t)size;
  return data;
}

int main(int argc, char **argv)
{
  IDXGIFactory4 *factory = NULL;
  IDXGIAdapter *adapter = NULL;
  ID3D12Device *device = NULL;
  ID3D12RootSignature *root = NULL;
  ID3D12PipelineState *pipeline = NULL;
  ID3D12Resource *input = NULL;
  ID3D12Resource *constants = NULL;
  ID3DBlob *serialized = NULL;
  ID3DBlob *errors = NULL;
  D3D12_ROOT_SIGNATURE_DESC root_desc;
  D3D12_ROOT_PARAMETER parameter[3];
  D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_desc;
  unsigned char *data = NULL;
  FILE *file;
  long size;
  HRESULT status = E_FAIL;
  uint32_t default_expected[32];
  uint32_t *expected = default_expected;
  size_t expected_bytes = sizeof(default_expected);
  float expected_float;
  unsigned i;
  if (argc != 2 && argc != 3 && argc != 5) return 2;
  file = fopen(argv[1], "rb");
  if (!file) return 1;
  if (fseek(file, 0, SEEK_END) || (size = ftell(file)) < 32 || fseek(file, 0, SEEK_SET)) {
    fclose(file); return 1;
  }
  data = (unsigned char *)malloc((size_t)size);
  if (!data) { fclose(file); return 1; }
  if (fread(data, 1, (size_t)size, file) != (size_t)size) {
    fclose(file); free(data); return 1;
  }
  fclose(file);
  for (i = 0; i < 32; ++i) {
    expected_float = (float)(i < 16 ? i + 1 : i * 3) + 0.25f;
    memcpy(&default_expected[i], &expected_float, sizeof(expected_float));
  }
  if (argc >= 3) {
    expected = read_expected(argv[2], &expected_bytes);
    if (!expected) { free(data); return 1; }
  }
  status = CreateDXGIFactory1(&IID_IDXGIFactory4, (void **)&factory);
  if (FAILED(status)) goto done;
  status = IDXGIFactory4_EnumWarpAdapter(factory, &IID_IDXGIAdapter, (void **)&adapter);
  if (FAILED(status)) goto done;
  status = D3D12CreateDevice((IUnknown *)adapter, D3D_FEATURE_LEVEL_11_0,
                            &IID_ID3D12Device, (void **)&device);
  if (FAILED(status)) goto done;
  memset(&root_desc, 0, sizeof(root_desc));
  memset(&parameter, 0, sizeof(parameter));
  parameter[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
  parameter[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  root_desc.NumParameters = argc == 5 ? 3 : 1; root_desc.pParameters = parameter;
  if (argc == 5) {
    parameter[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    parameter[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameter[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameter[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameter[2].Descriptor.ShaderRegister = 4; parameter[2].Descriptor.RegisterSpace = 1;
    status = upload_file(device, argv[3], &input);
    if (FAILED(status)) goto done;
    status = upload_file(device, argv[4], &constants);
    if (FAILED(status)) goto done;
  }
  status = D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1,
                                      &serialized, &errors);
  if (FAILED(status)) goto done;
  status = ID3D12Device_CreateRootSignature(device, 0,
      ID3D10Blob_GetBufferPointer(serialized), ID3D10Blob_GetBufferSize(serialized),
      &IID_ID3D12RootSignature, (void **)&root);
  if (FAILED(status)) goto done;
  memset(&pipeline_desc, 0, sizeof(pipeline_desc));
  pipeline_desc.pRootSignature = root;
  pipeline_desc.CS.pShaderBytecode = data;
  pipeline_desc.CS.BytecodeLength = (SIZE_T)size;
  status = ID3D12Device_CreateComputePipelineState(device, &pipeline_desc,
      &IID_ID3D12PipelineState, (void **)&pipeline);
  if (SUCCEEDED(status)) status = execute(device, root, pipeline,
      expected, expected_bytes, argc >= 3, input, constants);
done:
  printf("D3D12 WARP pipeline and execution: 0x%08lX\n", (unsigned long)status);
  if (pipeline) ID3D12PipelineState_Release(pipeline);
  if (root) ID3D12RootSignature_Release(root);
  if (constants) ID3D12Resource_Release(constants);
  if (input) ID3D12Resource_Release(input);
  if (errors) ID3D10Blob_Release(errors);
  if (serialized) ID3D10Blob_Release(serialized);
  if (device) ID3D12Device_Release(device);
  if (adapter) IDXGIAdapter_Release(adapter);
  if (factory) IDXGIFactory4_Release(factory);
  free(data);
  if (expected != default_expected) free(expected);
  return FAILED(status) ? 1 : 0;
}
