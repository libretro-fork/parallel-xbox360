/* Execute native ROV EDRAM depth/stencil shaders on WARP. MSVC C89. */
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
static uint32_t unorm24(float depth) {
  float scaled = depth*16777215.0f;
  uint32_t value = (uint32_t)scaled;
  float fraction = scaled-(float)value;
  return value + (fraction > 0.5f || (fraction == 0.5f && (value & 1)));
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
  ID3D12Resource *upload = NULL, *counter = NULL, *edram = NULL, *readback = NULL;
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
  uint32_t *values, *golden = NULL, expected, packed = 0x40000011;
  size_t ps_size = 0, vs_size = 0;
  UINT constants[16] = {0,0,0,0};
  unsigned x, y, sample, sub_x, sub_y, u, v, address, msaa, config, scenario, survey, reverse, sx, sy, width, height, words, bytes, tile_words, pitch, guest_width, guest_height;
  unsigned full_counters, slot, partial, covered_samples = 0, sample_x16, sample_y16, host_sample;
  static const int positions[4][2] = {{-2,-6},{6,-2},{-6,2},{2,6}};
  float sample_x[4] = {-0.125f,0.375f,-0.375f,0.125f}, slope, depth, swap[4];
  uint32_t depth_bits;
  HRESULT status = E_FAIL;
  if (argc != 7 && argc != 8) return 2;
  msaa = (unsigned)strtoul(argv[3], NULL, 10); config = (unsigned)strtoul(argv[4], NULL, 10);
  scenario = (unsigned)strtoul(argv[5], NULL, 10); survey = (unsigned)strtoul(argv[6], NULL, 10);
  if (msaa > 2 || config > 1 || scenario > 9 || survey > 1) return 2;
  reverse = argc == 8 && strstr(argv[7], "reverse") != NULL;
  full_counters = argc != 8 || strstr(argv[7], "nofail") == NULL;
  slot = argc == 8 && strstr(argv[7], "slot3") != NULL ? 3 : 0;
  partial = argc == 8 && strstr(argv[7], "partial") != NULL;
  if (partial && scenario >= 7) return 2;
  guest_width = scenario >= 8 ? 64 : 80; guest_height = scenario >= 8 ? 8 : 16;
  sx = config ? 2 : 1; sy = config ? 3 : 1; width = guest_width*sx; height = guest_height*sy;
  tile_words = 1280*sx*sy; words = 8*tile_words; bytes = words*4; pitch = msaa == 2 ? 2 : 1;
  golden = (uint32_t *)malloc(bytes); if (!golden) return 1;
  for (x = 0; x < words; ++x) golden[x] = 0xffffff11;
  constants[0] = (1u<<17) | (1u<<18) | (1u<<21);
  constants[1] = tile_words*pitch; constants[2] = tile_words;
  if (scenario == 1) constants[0] = (1u<<17) | (4u<<18) | (1u<<21);
  if (scenario == 2 || scenario == 3) {
    constants[0] |= 1u<<22;
    constants[8] = constants[9] = scenario == 2 ? 0xffff5a : 0x0fff5a;
    constants[10] = constants[11] = scenario == 2 ? 3u<<3 : 7u | (2u<<6);
    packed = scenario == 2 ? 0xffffff12 : 0x4000001a;
  }
  if (scenario == 4) constants[0] = 0;
  if (scenario == 5) { constants[0] |= 1u<<10; packed = 0xd0000011; }
  constants[3] = scenario == 6 ? UINT32_MAX : slot;
  if (scenario == 7) {
    constants[0] |= 1u<<22; constants[6] = 0x3e800000; constants[7] = 0xbe000000;
    constants[8] = 0xffff5a; constants[9] = 0xffffa5; constants[10] = constants[11] = 7u | (2u<<6);
    packed = reverse ? 0x200000a5 : 0x8000005a;
  }
  if (scenario >= 8) {
    constants[4] = 0x40800000; constants[5] = 0x41000000;
    if (scenario == 9) constants[0] |= 1u<<10;
  }
  {
    /* Independent full-tile mapping of guest samples, including the depth half swap. */
    for (y = 0; y < guest_height; ++y) for (x = 0; x < guest_width; ++x) for (sample = 0; sample < (1u<<msaa); ++sample)
      for (sub_y = 0; sub_y < sy; ++sub_y) for (sub_x = 0; sub_x < sx; ++sub_x) {
        if (partial) {
          host_sample = msaa == 1 && sample == 1 ? 3 : sample;
          sample_x16 = (unsigned)((int)((x*sx+sub_x)*16+8) + (msaa ? positions[host_sample][0] : 0));
          sample_y16 = (unsigned)((int)((y*sy+sub_y)*16+8) + (msaa ? positions[host_sample][1] : 0));
          if (sample_x16*height+sample_y16*width >= width*height*16) continue;
        }
        ++covered_samples;
        u = msaa == 0 ? x : msaa == 1 ? (x/4)*4+x%2+2*sample : (x/2)*4+x%2+2*(sample%2);
        v = msaa == 0 ? y : (y/2)*4+y%2+2*(msaa == 1 ? (x/2)%2 : sample/2);
        u = u*sx+sub_x; v = v*sy+sub_y;
        address = tile_words + ((v/(16*sy))*pitch + u/(80*sx))*tile_words;
        address += (v%(16*sy))*(80*sx) + (u%(80*sx)+40*sx)%(80*sx);
        if (address >= words) { free(golden); return 1; }
        if (scenario >= 8) {
          slope = 0.125f/(float)width;
          depth = 0.125f + slope*((float)(x*sx+sub_x)+0.5f);
          depth += slope*(reverse ? 8.0f : 4.0f);
          if (msaa) depth += slope*sample_x[msaa == 1 && sample == 1 ? 3 : sample];
          if (scenario == 9) {
            memcpy(&depth_bits, &depth, 4);
            packed = ((depth_bits-0x38000000+3+((depth_bits>>3)&1))>>3)<<8;
          } else packed = unorm24(depth)<<8;
          packed |= 17;
        }
        if (scenario != 1 && scenario != 4) golden[address] = packed;
      }
  }
  ps = read_binary(argv[1], &ps_size); vs = read_binary(argv[2], &vs_size);
  if (!ps || !vs) goto done;
#define CHECK(expression) do { status = (expression); if (FAILED(status)) { fprintf(stderr, "Failed at line %d\n", __LINE__); goto done; } } while (0)
  CHECK(CreateDXGIFactory1(&IID_IDXGIFactory4, (void **)&factory));
  CHECK(IDXGIFactory4_EnumWarpAdapter(factory, &IID_IDXGIAdapter, (void **)&adapter));
  CHECK(D3D12CreateDevice((IUnknown *)adapter, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&device));
  memset(&root_desc, 0, sizeof(root_desc)); memset(parameters, 0, sizeof(parameters)); memset(&srv_range, 0, sizeof(srv_range));
  srv_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV; srv_range.NumDescriptors = 2; srv_range.BaseShaderRegister = 1;
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[0].DescriptorTable.NumDescriptorRanges = 1; parameters[0].DescriptorTable.pDescriptorRanges = &srv_range;
  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[1].Constants.ShaderRegister = 0; parameters[1].Constants.RegisterSpace = 1; parameters[1].Constants.Num32BitValues = 16;
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
  desc.RasterizerState.ForcedSampleCount = msaa ? 4 : 1;
  CHECK(ID3D12Device_CreateGraphicsPipelineState(device, &desc, &IID_ID3D12PipelineState, (void **)&pipeline));
  CHECK(buffer(device, D3D12_HEAP_TYPE_UPLOAD, bytes+512, D3D12_RESOURCE_STATE_GENERIC_READ, &upload));
  CHECK(buffer(device, D3D12_HEAP_TYPE_READBACK, bytes+256, D3D12_RESOURCE_STATE_COPY_DEST, &readback));
  CHECK(buffer(device, D3D12_HEAP_TYPE_DEFAULT, 256, D3D12_RESOURCE_STATE_COPY_DEST, &counter));
  CHECK(buffer(device, D3D12_HEAP_TYPE_DEFAULT, bytes, D3D12_RESOURCE_STATE_COPY_DEST, &edram));
  memset(&range, 0, sizeof(range)); CHECK(ID3D12Resource_Map(upload, 0, &range, &mapped));
  memset(mapped, 0, bytes+512);
  for (x = 0; x < words; ++x) ((uint32_t *)mapped)[x] = 0xffffff11;
  memcpy((unsigned char *)mapped + bytes, vertices, sizeof(vertices));
  for (x = 0; x < 3; ++x) ((float *)((unsigned char *)mapped + bytes))[x*4+2] = 0.25f;
  if (scenario >= 8) {
    ((float *)((unsigned char *)mapped + bytes))[2] = 0.125f;
    ((float *)((unsigned char *)mapped + bytes))[6] = 0.375f;
    ((float *)((unsigned char *)mapped + bytes))[10] = 0.125f;
  }
  if (partial) {
    ((float *)((unsigned char *)mapped + bytes))[4] = 1.0f;
    ((float *)((unsigned char *)mapped + bytes))[9] = -1.0f;
  }
  if (reverse) {
    memcpy(swap, (unsigned char *)mapped+bytes, 16);
    memcpy((unsigned char *)mapped+bytes, (unsigned char *)mapped+bytes+16, 16);
    memcpy((unsigned char *)mapped+bytes+16, swap, 16);
  }
  ID3D12Resource_Unmap(upload, 0, NULL); mapped = NULL;
  memset(&heap_desc, 0, sizeof(heap_desc)); heap_desc.NumDescriptors = 2;
  heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  CHECK(ID3D12Device_CreateDescriptorHeap(device, &heap_desc, &IID_ID3D12DescriptorHeap, (void **)&srv_heap));
  ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(srv_heap, &cpu);
  memset(&uav, 0, sizeof(uav)); uav.Format = DXGI_FORMAT_R32_TYPELESS;
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER; uav.Buffer.NumElements = 64;
  uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
  ID3D12Device_CreateUnorderedAccessView(device, counter, NULL, &uav, cpu);
  cpu.ptr += ID3D12Device_GetDescriptorHandleIncrementSize(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  uav.Buffer.NumElements = words; ID3D12Device_CreateUnorderedAccessView(device, edram, NULL, &uav, cpu);
  memset(&queue_desc, 0, sizeof(queue_desc));
  CHECK(ID3D12Device_CreateCommandQueue(device, &queue_desc, &IID_ID3D12CommandQueue, (void **)&queue));
  CHECK(ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void **)&allocator));
  CHECK(ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, pipeline, &IID_ID3D12GraphicsCommandList, (void **)&list));
  ID3D12GraphicsCommandList_CopyBufferRegion(list, counter, 0, upload, bytes+256, 256);
  transition(list, counter, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  ID3D12GraphicsCommandList_CopyBufferRegion(list, edram, 0, upload, 0, bytes);
  transition(list, edram, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  ID3D12GraphicsCommandList_SetGraphicsRootSignature(list, root);
  ID3D12GraphicsCommandList_SetDescriptorHeaps(list, 1, &srv_heap);
  ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(srv_heap, &gpu);
  ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(list, 0, gpu);
  ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(list, 1, 16, constants, 0);
  memset(&viewport, 0, sizeof(viewport)); viewport.Width = (float)width; viewport.Height = (float)height; viewport.MaxDepth = 1;
  scissor.left = 0; scissor.top = 0; scissor.right = (LONG)width; scissor.bottom = (LONG)height;
  ID3D12GraphicsCommandList_RSSetViewports(list, 1, &viewport); ID3D12GraphicsCommandList_RSSetScissorRects(list, 1, &scissor);
  ID3D12GraphicsCommandList_OMSetRenderTargets(list, 0, NULL, FALSE, NULL);
  vertex_view.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(upload) + bytes;
  vertex_view.SizeInBytes = sizeof(vertices); vertex_view.StrideInBytes = 16;
  ID3D12GraphicsCommandList_IASetVertexBuffers(list, 0, 1, &vertex_view);
  ID3D12GraphicsCommandList_IASetPrimitiveTopology(list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  ID3D12GraphicsCommandList_DrawInstanced(list, 3, 1, 0, 0);
  transition(list, counter, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
  ID3D12GraphicsCommandList_CopyBufferRegion(list, readback, bytes, counter, 0, 256);
  transition(list, edram, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
  ID3D12GraphicsCommandList_CopyBufferRegion(list, readback, 0, edram, 0, bytes);
  CHECK(ID3D12GraphicsCommandList_Close(list));
  submitted = (ID3D12CommandList *)list; ID3D12CommandQueue_ExecuteCommandLists(queue, 1, &submitted);
  CHECK(ID3D12Device_CreateFence(device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&fence));
  event = CreateEventA(NULL, FALSE, FALSE, NULL); if (!event) { status = E_FAIL; goto done; }
  CHECK(ID3D12CommandQueue_Signal(queue, fence, 1)); CHECK(ID3D12Fence_SetEventOnCompletion(fence, 1, event));
  if (WaitForSingleObject(event, 10000) != WAIT_OBJECT_0) { status = E_FAIL; goto done; }
  range.Begin = 0; range.End = bytes+256; CHECK(ID3D12Resource_Map(readback, 0, &range, &mapped)); values = (uint32_t *)mapped;
  for (x = 0; x < words; ++x) if (values[x] != golden[x]) {
    fprintf(stderr, "EDRAM word %u mismatch: %08lX != %08lX\n", x, (unsigned long)values[x], (unsigned long)golden[x]); status = E_FAIL; goto done;
  }
  values += words;
  for (x = 0; x < 64; ++x) {
    expected = 0;
    if (scenario != 6) {
      if (x == slot*4+2 && scenario != 1 && scenario != 2) expected = survey ? 1 : covered_samples;
      if (full_counters && !survey && ((x == slot*4+1 && scenario == 1) || (x == slot*4+3 && scenario == 2))) expected = covered_samples;
    }
    if (values[x] != expected) { fprintf(stderr, "Counter %u mismatch: %lu != %lu\n", x, (unsigned long)values[x], (unsigned long)expected); status = E_FAIL; goto done; }
  }
  puts("Native ROV depth: all EDRAM words and query counters matched.");
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
  if (edram) ID3D12Resource_Release(edram);
  if (upload) ID3D12Resource_Release(upload);
  if (root) ID3D12RootSignature_Release(root);
  if (errors) ID3D10Blob_Release(errors);
  if (serialized) ID3D10Blob_Release(serialized);
  if (device) ID3D12Device_Release(device);
  if (adapter) IDXGIAdapter_Release(adapter);
  if (factory) IDXGIFactory4_Release(factory);
  free(vs); free(ps); free(golden);
  return FAILED(status) ? 1 : 0;
}
