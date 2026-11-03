/* C89 fixture: ask D3D12 WARP to accept the native DXIL container. */
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#ifdef __MINGW32__
#include <directx/d3d12.h>
#include <directx/d3d12sdklayers.h>
#else
#include <d3d12.h>
#include <d3d12sdklayers.h>
#endif
#include <dxgi1_4.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <float.h>
#include <math.h>

typedef struct TextureFixture {
  ID3D12Resource *texture, *upload;
  ID3D12DescriptorHeap *heap, *sampler_heap;
  unsigned heap_mode, array_mode, policy_mode;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprints[2];
} TextureFixture;

static void release_texture(TextureFixture *fixture) {
  if (fixture->sampler_heap) ID3D12DescriptorHeap_Release(fixture->sampler_heap);
  if (fixture->heap) ID3D12DescriptorHeap_Release(fixture->heap);
  if (fixture->upload) ID3D12Resource_Release(fixture->upload);
  if (fixture->texture) ID3D12Resource_Release(fixture->texture);
}

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

/* Two RGBA32F mips: 4x4 followed by 2x2. Read rows straight into the
 * D3D12 upload footprints, avoiding a staging copy on the CPU. */
static HRESULT prepare_texture(ID3D12Device *device, const char *path, TextureFixture *fixture) {
  D3D12_HEAP_PROPERTIES heap;
  D3D12_RESOURCE_DESC desc;
  D3D12_DESCRIPTOR_HEAP_DESC heap_desc;
  D3D12_SHADER_RESOURCE_VIEW_DESC view;
  D3D12_SAMPLER_DESC sampler;
  D3D12_RANGE range;
  D3D12_CPU_DESCRIPTOR_HANDLE cpu;
  UINT rows[2];
  UINT64 row_sizes[2], size;
  unsigned char *mapped;
  unsigned mip, row;
  FILE *file;
  HRESULT status;
  memset(&heap, 0, sizeof(heap)); heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  heap.CreationNodeMask = 1; heap.VisibleNodeMask = 1;
  memset(&desc, 0, sizeof(desc)); desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = 4; desc.Height = 4; desc.DepthOrArraySize = 1; desc.MipLevels = 2;
  desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT; desc.SampleDesc.Count = 1;
  status = ID3D12Device_CreateCommittedResource(device, &heap, D3D12_HEAP_FLAG_NONE,
    &desc, D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void **)&fixture->texture);
  if (FAILED(status)) return status;
  ID3D12Device_GetCopyableFootprints(device, &desc, 0, 2, 0, fixture->footprints, rows, row_sizes, &size);
  if (size > (UINT64)(size_t)-1) return E_FAIL;
  status = buffer(device, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, (size_t)size, &fixture->upload);
  if (FAILED(status)) return status;
  file = fopen(path, "rb");
  if (!file) return E_FAIL;
  range.Begin = 0; range.End = 0;
  status = ID3D12Resource_Map(fixture->upload, 0, &range, (void **)&mapped);
  if (FAILED(status)) { fclose(file); return status; }
  memset(mapped, 0, (size_t)size);
  for (mip = 0; mip < 2; ++mip) for (row = 0; row < rows[mip]; ++row) {
    if (fread(mapped + (size_t)fixture->footprints[mip].Offset +
        (size_t)row * fixture->footprints[mip].Footprint.RowPitch, 1,
        (size_t)row_sizes[mip], file) != (size_t)row_sizes[mip]) status = E_FAIL;
  }
  if (fgetc(file) != EOF || ferror(file)) status = E_FAIL;
  if (fclose(file)) status = E_FAIL;
  range.End = (SIZE_T)size; ID3D12Resource_Unmap(fixture->upload, 0, &range);
  if (FAILED(status)) return status;
  memset(&heap_desc, 0, sizeof(heap_desc));
  heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  heap_desc.NumDescriptors = fixture->heap_mode ? 4 : 1; heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  status = ID3D12Device_CreateDescriptorHeap(device, &heap_desc, &IID_ID3D12DescriptorHeap, (void **)&fixture->heap);
  if (FAILED(status)) return status;
  memset(&view, 0, sizeof(view)); view.Format = desc.Format;
  view.ViewDimension = fixture->array_mode ? D3D12_SRV_DIMENSION_TEXTURE2DARRAY : D3D12_SRV_DIMENSION_TEXTURE2D;
  if (fixture->array_mode) view.Texture2DArray.ArraySize = 1;
  view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  view.Texture2D.MipLevels = 2;
  ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(fixture->heap, &cpu);
  for (mip = 0; mip < (fixture->heap_mode ? 4u : 1u); ++mip) {
    view.Texture2D.MostDetailedMip = fixture->heap_mode && mip == 2 ? 1 : 0;
    view.Texture2D.MipLevels = fixture->heap_mode && (mip == 1 || mip == 2) ? 1 : 2;
    ID3D12Device_CreateShaderResourceView(device, fixture->texture, &view, cpu);
    cpu.ptr += ID3D12Device_GetDescriptorHandleIncrementSize(device,D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  }
  if (fixture->heap_mode) {
    heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER; heap_desc.NumDescriptors = 2;
    status = ID3D12Device_CreateDescriptorHeap(device,&heap_desc,&IID_ID3D12DescriptorHeap,(void **)&fixture->sampler_heap);
    if (FAILED(status)) return status;
    ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(fixture->sampler_heap,&cpu);
    memset(&sampler,0,sizeof(sampler)); sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxAnisotropy = 1; sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER; sampler.MaxLOD = FLT_MAX;
    for (mip = 0; mip < 2; ++mip) {
      sampler.Filter = mip ? D3D12_FILTER_MIN_MAG_MIP_LINEAR : D3D12_FILTER_MIN_MAG_MIP_POINT;
      ID3D12Device_CreateSampler(device,&sampler,cpu);
      cpu.ptr += ID3D12Device_GetDescriptorHandleIncrementSize(device,D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    }
  }
  return S_OK;
}

static HRESULT execute(ID3D12Device *device, ID3D12RootSignature *root,
                        ID3D12PipelineState *pipeline, const uint32_t *expected,
                        size_t bytes, int golden_records,
                        ID3D12Resource *input, ID3D12Resource *constants, TextureFixture *texture,
                        unsigned raw_mode, unsigned program_mode)
{
  ID3D12Resource *output = NULL;
  ID3D12Resource *readback = NULL;
  ID3D12Resource *initial = NULL;
  ID3D12CommandQueue *queue = NULL;
  ID3D12CommandAllocator *allocator = NULL;
  ID3D12GraphicsCommandList *list = NULL;
  ID3D12Fence *fence = NULL;
  ID3D12CommandList *commands[1];
  ID3D12DescriptorHeap *heaps[2];
  D3D12_COMMAND_QUEUE_DESC queue_desc;
  D3D12_RESOURCE_BARRIER barrier;
  D3D12_RANGE range;
  D3D12_TEXTURE_COPY_LOCATION source, destination;
  D3D12_GPU_DESCRIPTOR_HANDLE gpu;
  unsigned mip;
  HANDLE event = NULL;
  HRESULT status;
  uint32_t *values = NULL;
  void *initial_data;
  size_t i, source_index, allocation_bytes = raw_mode > 2 ? bytes / 8 * raw_mode : bytes;
  int same;
  float actual_float, expected_float;
  status = buffer(device, D3D12_HEAP_TYPE_DEFAULT,
                   raw_mode == 2 ? D3D12_RESOURCE_STATE_COPY_DEST : D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                   allocation_bytes, &output);
  if (FAILED(status)) goto done;
  if (raw_mode == 2) {
    status = buffer(device, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, allocation_bytes, &initial);
    if (FAILED(status)) goto done;
    range.Begin = 0; range.End = 0;
    status = ID3D12Resource_Map(initial, 0, &range, &initial_data);
    if (FAILED(status)) goto done;
    memset(initial_data, 0, allocation_bytes); ID3D12Resource_Unmap(initial, 0, NULL);
  }
  status = buffer(device, D3D12_HEAP_TYPE_READBACK,
                   D3D12_RESOURCE_STATE_COPY_DEST, allocation_bytes, &readback);
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
  if (initial) {
    ID3D12GraphicsCommandList_CopyResource(list, output, initial);
    memset(&barrier, 0, sizeof(barrier)); barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = output; barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &barrier);
  }
  if (texture) {
    for (mip = 0; mip < 2; ++mip) {
      memset(&source, 0, sizeof(source)); memset(&destination, 0, sizeof(destination));
      source.pResource = texture->upload; source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
      source.PlacedFootprint = texture->footprints[mip];
      destination.pResource = texture->texture; destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
      destination.SubresourceIndex = mip;
      ID3D12GraphicsCommandList_CopyTextureRegion(list, &destination, 0, 0, 0, &source, NULL);
    }
    memset(&barrier, 0, sizeof(barrier)); barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = texture->texture;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &barrier);
    heaps[0] = texture->heap; heaps[1] = texture->sampler_heap;
    ID3D12GraphicsCommandList_SetDescriptorHeaps(list, texture->heap_mode ? 2 : 1, heaps);
  }
  ID3D12GraphicsCommandList_SetComputeRootSignature(list, root);
  ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(list, 0,
      ID3D12Resource_GetGPUVirtualAddress(output));
  if (input && constants) {
    ID3D12GraphicsCommandList_SetComputeRootShaderResourceView(list, 1,
        ID3D12Resource_GetGPUVirtualAddress(input));
    ID3D12GraphicsCommandList_SetComputeRootConstantBufferView(list, 2,
        ID3D12Resource_GetGPUVirtualAddress(constants));
    if (program_mode) {
      ID3D12GraphicsCommandList_SetComputeRootConstantBufferView(list, 3, ID3D12Resource_GetGPUVirtualAddress(constants)+4096);
      ID3D12GraphicsCommandList_SetComputeRootConstantBufferView(list, 4, ID3D12Resource_GetGPUVirtualAddress(constants)+4352);
    }
  }
  if (texture) {
    ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(texture->heap, &gpu);
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(list, 1, gpu);
    if (constants) ID3D12GraphicsCommandList_SetComputeRootConstantBufferView(list, 2,
      ID3D12Resource_GetGPUVirtualAddress(constants));
    if (texture->heap_mode) ID3D12GraphicsCommandList_SetComputeRootShaderResourceView(list,3,
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
  if (WaitForSingleObject(event, 50000) != WAIT_OBJECT_0) { status = E_FAIL; goto done; }
  range.Begin = 0; range.End = allocation_bytes;
  status = ID3D12Resource_Map(readback, 0, &range, (void **)&values);
  if (FAILED(status)) goto done;
  for (i = 0; i < bytes / sizeof(uint32_t); ++i) {
    source_index = raw_mode > 2 ? (i / 8) * raw_mode + i % 8 : i;
    same = values[source_index] == expected[i];
    if (!raw_mode && (!golden_records || i % 8 < 4) &&
        (expected[i] & UINT32_C(0x7f800000)) == UINT32_C(0x7f800000) &&
        (expected[i] & UINT32_C(0x007fffff)))
      same = (values[i] & UINT32_C(0x7f800000)) == UINT32_C(0x7f800000) &&
        (values[i] & UINT32_C(0x007fffff));
    if (!same && texture && texture->policy_mode && i%8 < 4) {
      memcpy(&actual_float,values+source_index,4); memcpy(&expected_float,expected+i,4);
      same = fabs((double)actual_float-expected_float) <= .000001*(fabs((double)expected_float)+1);
    }
    if (!same) {
      fprintf(stderr, "word %lu: got %08lX, expected %08lX\n", (unsigned long)i,
        (unsigned long)values[source_index], (unsigned long)expected[i]);
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
  if (initial) ID3D12Resource_Release(initial);
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
  ID3D12Debug *debug = NULL;
  ID3D12InfoQueue *info = NULL;
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
  D3D12_ROOT_PARAMETER parameter[5];
  D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_desc;
  D3D12_DESCRIPTOR_RANGE texture_range;
  D3D12_STATIC_SAMPLER_DESC samplers[2];
  TextureFixture texture;
  unsigned char *data = NULL;
  FILE *file;
  long size;
  HRESULT status = E_FAIL;
  uint32_t default_expected[32];
  uint32_t *expected = default_expected;
  size_t expected_bytes = sizeof(default_expected);
  float expected_float;
  unsigned i, raw_mode, program_mode, resource_mode;
  memset(&texture, 0, sizeof(texture));
  if (argc < 2 || argc > 6) return 2;
  program_mode = argc == 6 && !strcmp(argv[5], "program");
  resource_mode = argc == 6 && !strcmp(argv[5], "resource-raw");
  texture.policy_mode = argc == 6 && !strcmp(argv[5], "heap-policy");
  texture.array_mode = texture.policy_mode || (argc == 6 && !strcmp(argv[5], "heap-array"));
  texture.heap_mode = texture.array_mode || (argc == 6 && !strcmp(argv[5], "heap"));
  raw_mode = program_mode || resource_mode || texture.heap_mode ? 1 : argc == 6 ? !strcmp(argv[5], "resolve") ? 2 : 80 : argc == 4 && !strcmp(argv[3], "raw") ? 1 : 0;
  if (argc == 6 && !program_mode && !resource_mode && !texture.heap_mode && strcmp(argv[5], "dump") && strcmp(argv[5], "resolve")) return 2;
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
  if (SUCCEEDED(D3D12GetDebugInterface(&IID_ID3D12Debug,(void **)&debug))) ID3D12Debug_EnableDebugLayer(debug);
  status = CreateDXGIFactory1(&IID_IDXGIFactory4, (void **)&factory);
  if (FAILED(status)) goto done;
  status = IDXGIFactory4_EnumWarpAdapter(factory, &IID_IDXGIAdapter, (void **)&adapter);
  if (FAILED(status)) goto done;
  status = D3D12CreateDevice((IUnknown *)adapter, D3D_FEATURE_LEVEL_11_0,
                            &IID_ID3D12Device, (void **)&device);
  if (FAILED(status)) goto done;
  ID3D12Device_QueryInterface(device,&IID_ID3D12InfoQueue,(void **)&info);
  memset(&root_desc, 0, sizeof(root_desc));
  if (texture.heap_mode) root_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
    D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED;
  memset(&parameter, 0, sizeof(parameter));
  parameter[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
  parameter[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  root_desc.NumParameters = argc == 5 || program_mode || resource_mode ? 3 : 1; root_desc.pParameters = parameter;
  if ((argc == 4 && !raw_mode) || (argc == 6 && !program_mode && !resource_mode)) {
    memset(&texture_range, 0, sizeof(texture_range)); texture_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    texture_range.NumDescriptors = 1; texture_range.RegisterSpace = texture.policy_mode ? 2 : 0;
    parameter[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameter[1].DescriptorTable.NumDescriptorRanges = 1;
    parameter[1].DescriptorTable.pDescriptorRanges = &texture_range;
    root_desc.NumParameters = 2;
    memset(samplers, 0, sizeof(samplers));
    for (i = 0; i < 2; ++i) {
      samplers[i].Filter = i ? D3D12_FILTER_MIN_MAG_MIP_LINEAR : D3D12_FILTER_MIN_MAG_MIP_POINT;
      samplers[i].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
      samplers[i].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
      samplers[i].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
      samplers[i].MaxAnisotropy = 1; samplers[i].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
      samplers[i].MaxLOD = FLT_MAX; samplers[i].ShaderRegister = i; samplers[i].RegisterSpace = texture.policy_mode ? 2 : 0;
      samplers[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    root_desc.NumStaticSamplers = 2; root_desc.pStaticSamplers = samplers;
    status = prepare_texture(device, argv[3], &texture);
    if (FAILED(status)) goto done;
    if (argc == 6) {
      root_desc.NumParameters = 3;
      parameter[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
      parameter[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
      parameter[2].Descriptor.ShaderRegister = 1; parameter[2].Descriptor.RegisterSpace = 31;
      status = upload_file(device, argv[4], &constants);
      if (FAILED(status)) goto done;
      if (texture.heap_mode) {
        parameter[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        parameter[3].Descriptor.ShaderRegister = texture.policy_mode ? 2 : 3;
        parameter[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        root_desc.NumParameters = 4;
      }
    }
  }
  if (argc == 5 || program_mode || resource_mode) {
    parameter[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    parameter[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameter[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameter[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameter[2].Descriptor.ShaderRegister = program_mode ? 1 : 4; parameter[2].Descriptor.RegisterSpace = 1;
    if (program_mode) {
      parameter[3] = parameter[2]; parameter[3].Descriptor.ShaderRegister = 3;
      parameter[4] = parameter[2]; parameter[4].Descriptor.ShaderRegister = 4;
      root_desc.NumParameters = 5;
    }
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
      expected, expected_bytes, argc >= 3, input, constants,
      (argc == 4 && !raw_mode) || (argc == 6 && !program_mode && !resource_mode) ? &texture : NULL, raw_mode, program_mode);
done:
  if (info && FAILED(status)) {
    UINT64 message_index;
    for (message_index = 0; message_index < ID3D12InfoQueue_GetNumStoredMessages(info); ++message_index) {
      SIZE_T length = 0; D3D12_MESSAGE *message;
      ID3D12InfoQueue_GetMessage(info,message_index,NULL,&length);
      message = (D3D12_MESSAGE *)malloc(length);
      if (message) {
        if (SUCCEEDED(ID3D12InfoQueue_GetMessage(info,message_index,message,&length))) fprintf(stderr,"%s\n",message->pDescription);
        free(message);
      }
    }
  }
  if (info) ID3D12InfoQueue_Release(info);
  if (debug) ID3D12Debug_Release(debug);
  printf("D3D12 WARP pipeline and execution: 0x%08lX\n", (unsigned long)status);
  if (pipeline) ID3D12PipelineState_Release(pipeline);
  if (root) ID3D12RootSignature_Release(root);
  if (constants) ID3D12Resource_Release(constants);
  if (input) ID3D12Resource_Release(input);
  release_texture(&texture);
  if (errors) ID3D10Blob_Release(errors);
  if (serialized) ID3D10Blob_Release(serialized);
  if (device) ID3D12Device_Release(device);
  if (adapter) IDXGIAdapter_Release(adapter);
  if (factory) IDXGIFactory4_Release(factory);
  free(data);
  if (expected != default_expected) free(expected);
  return FAILED(status) ? 1 : 0;
}
