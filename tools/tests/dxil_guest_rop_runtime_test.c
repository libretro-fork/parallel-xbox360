/* Execute complete guest ROV EDRAM shaders on WARP. MSVC C89. */
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
#include <math.h>

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
static HRESULT sample_texture(ID3D12Device *device, ID3D12Resource **texture,
    ID3D12Resource **upload, D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprints[2], unsigned width, unsigned height) {
  D3D12_HEAP_PROPERTIES heap; D3D12_RESOURCE_DESC desc; D3D12_RANGE range;
  UINT rows[2]; UINT64 row_sizes[2], bytes; void *mapped; unsigned mip, x, y, j; HRESULT status;
  memset(&heap,0,sizeof(heap)); heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  memset(&desc,0,sizeof(desc)); desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; desc.Width = width; desc.Height = height;
  desc.DepthOrArraySize = 1; desc.MipLevels = 2; desc.SampleDesc.Count = 1; desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
  status = ID3D12Device_CreateCommittedResource(device,&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,NULL,&IID_ID3D12Resource,(void **)texture);
  if (FAILED(status)) return status;
  ID3D12Device_GetCopyableFootprints(device,&desc,0,2,0,footprints,rows,row_sizes,&bytes);
  status = buffer(device,D3D12_HEAP_TYPE_UPLOAD,bytes,D3D12_RESOURCE_STATE_GENERIC_READ,upload); if (FAILED(status)) return status;
  memset(&range,0,sizeof(range)); status = ID3D12Resource_Map(*upload,0,&range,&mapped); if (FAILED(status)) return status;
  memset(mapped,0,(size_t)bytes);
  for (mip = 0; mip < 2; ++mip) for (y = 0; y < (mip ? height/2 : height); ++y) for (x = 0; x < (mip ? width/2 : width); ++x) {
    float *pixel = (float *)((unsigned char *)mapped+(size_t)footprints[mip].Offset+y*footprints[mip].Footprint.RowPitch+x*16);
    for (j = 0; j < 4; ++j) pixel[j] = mip ? (float)(20+(y*(width/2)+x)*4+j) : j == 0 ? (float)(x+1) : j == 1 ? (float)(y+1) : j == 2 ? (float)(y*width+x+1) : 1;
  }
  ID3D12Resource_Unmap(*upload,0,NULL); return S_OK;
}
static void transition(ID3D12GraphicsCommandList *list, ID3D12Resource *resource,
                          D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
  D3D12_RESOURCE_BARRIER barrier;
  memset(&barrier, 0, sizeof(barrier)); barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = resource; barrier.Transition.StateBefore = before;
  barrier.Transition.StateAfter = after; barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &barrier);
}
/* A masked float may jump far from the numeric oracle when an intrinsic differs
 * by one ULP. Check that its written bits can come from the allowed LOD range;
 * masked destination bits are checked separately and remain exact. */
static int masked_interval(uint32_t actual, uint32_t mask, uint32_t lo, uint32_t hi) {
  uint32_t bit, higher, candidate, swap; unsigned i;
  if (lo > hi) { swap = lo; lo = hi; hi = swap; }
  if ((lo&~mask) == (actual&~mask)) return 1;
  for (i = 0; i < 32; ++i) {
    bit = (uint32_t)1<<i; higher = ~(bit|(bit-1));
    if (!(lo&bit) && ((mask|actual)&bit) && !((lo^actual)&~mask&higher)) {
      candidate = (lo&higher)|bit|(actual&~mask&(bit-1));
      if (candidate <= hi) return 1;
    }
  }
  return 0;
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
  D3D12_ROOT_PARAMETER parameters[10];
  D3D12_DESCRIPTOR_RANGE texture_range, sampler_range;
  ID3D12Resource *sampled_texture = NULL, *sampled_upload = NULL; ID3D12DescriptorHeap *sampler_heap = NULL, *sampled_heaps[2];
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT sampled_footprints[2]; D3D12_TEXTURE_COPY_LOCATION source, destination;
  D3D12_SHADER_RESOURCE_VIEW_DESC sampled_view; D3D12_SAMPLER_DESC sampled_sampler;
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
  uint32_t *values, *golden = NULL, *initial = NULL;
  size_t ps_size = 0, vs_size = 0, bytes = 0, fixture_size = 0;
  uint32_t lod_lo_bits = 0, lod_hi_bits = 0, preserve;
  unsigned i, constants[128], counters[64], x, j, words, msaa, config, width, height;
  float colors[20], lod_bias, lod_lo, lod_hi;
  char *name = NULL;
  HRESULT status = E_FAIL;
  if (argc != 4) return 2;
  config = (unsigned)strtoul(argv[3],NULL,10); msaa = config&3; if (msaa > 2) return 2;
  width = config&4 ? 160 : 80; height = config&4 ? 48 : 16;
  name = (char *)malloc(strlen(argv[1])+16); if (!name) goto done;
#define READ(suffix, target, length) do { strcpy(name,argv[1]); strcat(name,suffix); target = read_binary(name,&length); if (!target) goto done; } while (0)
  READ(".initial",initial,bytes); READ(".expected",golden,fixture_size); if (fixture_size != bytes) goto done;
  {
    void *part;
    READ(".constants",part,fixture_size); if (fixture_size != sizeof(constants)) { free(part); goto done; } memcpy(constants,part,fixture_size); free(part);
    READ(".colors",part,fixture_size); if (fixture_size != sizeof(colors)) { free(part); goto done; } memcpy(colors,part,fixture_size); free(part);
    READ(".counters",part,fixture_size); if (fixture_size != sizeof(counters)) { free(part); goto done; } memcpy(counters,part,fixture_size); free(part);
  }
  if (constants[100] >= 1143) { width = config&4 ? 128 : 64; height = config&4 ? 24 : 8; }
  if (constants[101] == 16) {
    memcpy(&lod_bias,constants+7,4);
    lod_lo = (float)(log(4.0/(double)height)/log(2.0))*lod_bias;
    lod_hi = lod_lo+.008f*(float)fabs((double)lod_bias); lod_lo -= .008f*(float)fabs((double)lod_bias);
    memcpy(&lod_lo_bits,&lod_lo,4); memcpy(&lod_hi_bits,&lod_hi,4);
  }
  words = (unsigned)(bytes/4);
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
  for (j = 1; j < 5; ++j) { parameters[j].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; parameters[j].Descriptor.ShaderRegister = j == 1 ? 0 : j; parameters[j].Descriptor.RegisterSpace = 1; }
  parameters[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV; parameters[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; parameters[6].Descriptor.ShaderRegister = 1; parameters[6].Descriptor.RegisterSpace = 1; root_desc.NumParameters = 7; root_desc.pParameters = parameters;
  root_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
  if (constants[101] >= 4) {
    root_desc.Flags |= D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED | D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED;
    parameters[7].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV; parameters[7].Descriptor.ShaderRegister = 3;
    memset(&texture_range,0,sizeof(texture_range)); texture_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; texture_range.NumDescriptors = 1; texture_range.RegisterSpace = 3;
    sampler_range = texture_range; sampler_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    parameters[8].ParameterType = parameters[9].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[8].DescriptorTable.NumDescriptorRanges = parameters[9].DescriptorTable.NumDescriptorRanges = 1;
    parameters[8].DescriptorTable.pDescriptorRanges = &texture_range; parameters[9].DescriptorTable.pDescriptorRanges = &sampler_range;
    root_desc.NumParameters = 10;
  }
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
  CHECK(buffer(device, D3D12_HEAP_TYPE_UPLOAD, bytes+4096, D3D12_RESOURCE_STATE_GENERIC_READ, &upload));
  CHECK(buffer(device, D3D12_HEAP_TYPE_READBACK, bytes+256, D3D12_RESOURCE_STATE_COPY_DEST, &readback));
  CHECK(buffer(device, D3D12_HEAP_TYPE_DEFAULT, 256, D3D12_RESOURCE_STATE_COPY_DEST, &counter));
  CHECK(buffer(device, D3D12_HEAP_TYPE_DEFAULT, bytes, D3D12_RESOURCE_STATE_COPY_DEST, &edram));
  memset(&range, 0, sizeof(range)); CHECK(ID3D12Resource_Map(upload, 0, &range, &mapped));
  memset(mapped, 0, bytes+4096);
  memcpy(mapped,initial,bytes); memcpy((unsigned char *)mapped+bytes,vertices,sizeof(vertices));
  for (x = 0; x < 3; ++x) ((float *)((unsigned char *)mapped+bytes))[x*4+2] = .25f;
  constants[20] = constants[21] = constants[22] = 0x3f800000;
  if (constants[100] >= 1143) {
    float *positions = (float *)((unsigned char *)mapped+bytes);
    positions[2] = .125f; positions[6] = .375f; positions[10] = .125f;
    if (constants[100] == 1144) { float swap[4]; memcpy(swap,positions,16); memcpy(positions,positions+4,16); memcpy(positions+4,swap,16); }
  }
  memcpy((unsigned char *)mapped+bytes+512,constants,sizeof(constants));
  memcpy((unsigned char *)mapped+bytes+2048,(unsigned char *)mapped+bytes,sizeof(vertices));
  ((uint32_t *)((unsigned char *)mapped+bytes+1536))[0] = 2;
  ((uint32_t *)((unsigned char *)mapped+bytes+1536))[1] = sizeof(vertices);
  if (constants[101]) ((uint32_t *)((unsigned char *)mapped+bytes+1536))[2] = 3u|(3u<<13);
  {
    static const float vs_constants[12] = {.5f,-.5f,0,0,.5f,.5f,3,4,4,4,4,4};
    memcpy((unsigned char *)mapped+bytes+2560,vs_constants,sizeof(vs_constants));
    if (config&524288) ((float *)((unsigned char *)mapped+bytes+2560))[6] = .3125f;
    if (config&2048) { float *bias = (float *)((unsigned char *)mapped+bytes+2560); bias[4] += 1.25f/(float)width; bias[5] += .75f/(float)height; }
  }
  if (constants[101] >= 4) {
    uint32_t *indices = (uint32_t *)((unsigned char *)mapped+bytes+2816);
    indices[0] = 2; indices[2] = 3;
    indices = (uint32_t *)((unsigned char *)mapped+bytes+1536); indices[8] = 3u|(3u<<13); indices[10] = 1u<<6;
  }
  memcpy((unsigned char *)mapped+bytes+1024,colors,sizeof(colors));
  ID3D12Resource_Unmap(upload, 0, NULL); mapped = NULL;
  memset(&heap_desc, 0, sizeof(heap_desc)); heap_desc.NumDescriptors = constants[101] >= 4 ? 4 : 2;
  heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  CHECK(ID3D12Device_CreateDescriptorHeap(device, &heap_desc, &IID_ID3D12DescriptorHeap, (void **)&srv_heap));
  ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(srv_heap, &cpu);
  memset(&uav, 0, sizeof(uav)); uav.Format = DXGI_FORMAT_R32_TYPELESS;
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER; uav.Buffer.NumElements = 64;
  uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
  ID3D12Device_CreateUnorderedAccessView(device, counter, NULL, &uav, cpu);
  cpu.ptr += ID3D12Device_GetDescriptorHandleIncrementSize(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  uav.Buffer.NumElements = words; ID3D12Device_CreateUnorderedAccessView(device, edram, NULL, &uav, cpu);
  if (constants[101] >= 4) {
    CHECK(sample_texture(device,&sampled_texture,&sampled_upload,sampled_footprints,config&4096 ? 8 : 4,config&4096 ? 12 : 4));
    memset(&sampled_view,0,sizeof(sampled_view)); sampled_view.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    sampled_view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY; sampled_view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sampled_view.Texture2DArray.MipLevels = 2; sampled_view.Texture2DArray.ArraySize = 1;
    for (i = 0; i < 2; ++i) { cpu.ptr += ID3D12Device_GetDescriptorHandleIncrementSize(device,D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV); ID3D12Device_CreateShaderResourceView(device,sampled_texture,&sampled_view,cpu); }
    heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER; heap_desc.NumDescriptors = 1;
    CHECK(ID3D12Device_CreateDescriptorHeap(device,&heap_desc,&IID_ID3D12DescriptorHeap,(void **)&sampler_heap));
    ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(sampler_heap,&cpu);
    memset(&sampled_sampler,0,sizeof(sampled_sampler)); sampled_sampler.Filter = constants[101] == 16 ? D3D12_FILTER_MIN_MAG_MIP_LINEAR : D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampled_sampler.AddressU = sampled_sampler.AddressV = sampled_sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampled_sampler.MaxAnisotropy = 1; sampled_sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER; sampled_sampler.MaxLOD = 1000;
    ID3D12Device_CreateSampler(device,&sampled_sampler,cpu);
  }
  memset(&queue_desc, 0, sizeof(queue_desc));
  CHECK(ID3D12Device_CreateCommandQueue(device, &queue_desc, &IID_ID3D12CommandQueue, (void **)&queue));
  CHECK(ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void **)&allocator));
  CHECK(ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, pipeline, &IID_ID3D12GraphicsCommandList, (void **)&list));
  ID3D12GraphicsCommandList_CopyBufferRegion(list, counter, 0, upload, bytes+256, 256);
  transition(list, counter, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  ID3D12GraphicsCommandList_CopyBufferRegion(list, edram, 0, upload, 0, bytes);
  transition(list, edram, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  ID3D12GraphicsCommandList_SetGraphicsRootSignature(list, root);
  if (constants[101] >= 4) {
    for (i = 0; i < 2; ++i) { memset(&source,0,sizeof(source)); source.pResource = sampled_upload; source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; source.PlacedFootprint = sampled_footprints[i]; memset(&destination,0,sizeof(destination)); destination.pResource = sampled_texture; destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; destination.SubresourceIndex = i; ID3D12GraphicsCommandList_CopyTextureRegion(list,&destination,0,0,0,&source,NULL); }
    transition(list,sampled_texture,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    sampled_heaps[0] = srv_heap; sampled_heaps[1] = sampler_heap; ID3D12GraphicsCommandList_SetDescriptorHeaps(list,2,sampled_heaps);
    ID3D12GraphicsCommandList_SetGraphicsRootShaderResourceView(list,7,ID3D12Resource_GetGPUVirtualAddress(upload)+bytes+2816);
    ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(srv_heap,&gpu); gpu.ptr += 2*ID3D12Device_GetDescriptorHandleIncrementSize(device,D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV); ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(list,8,gpu);
    ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(sampler_heap,&gpu); ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(list,9,gpu);
  } else ID3D12GraphicsCommandList_SetDescriptorHeaps(list,1,&srv_heap);

  ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(srv_heap, &gpu);
  ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(list, 0, gpu);
  for (j = 1; j < 5; ++j) ID3D12GraphicsCommandList_SetGraphicsRootConstantBufferView(list,j,ID3D12Resource_GetGPUVirtualAddress(upload)+bytes+(j == 1 ? 512 : j == 2 ? 1024 : j == 3 ? 1280 : 1536));
  ID3D12GraphicsCommandList_SetGraphicsRootShaderResourceView(list,5,ID3D12Resource_GetGPUVirtualAddress(upload)+bytes+2048);
  ID3D12GraphicsCommandList_SetGraphicsRootConstantBufferView(list,6,ID3D12Resource_GetGPUVirtualAddress(upload)+bytes+2560);
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
    if (constants[101] && constants[102] >= 14 && golden[x] != initial[x] && x >= constants[43] && x < constants[43]+bytes/24) {
      float actual, expected;
      if (constants[101] == 16) {
        preserve = constants[54];
        if ((constants[102] != 15 || !((x-constants[43])&1)) &&
            !((values[x]^initial[x])&preserve) && masked_interval(values[x],preserve,lod_lo_bits,lod_hi_bits)) continue;
      } else {
        memcpy(&actual,values+x,4); memcpy(&expected,golden+x,4);
        if (fabs((double)actual-expected) <= .00001) continue;
      }
    }
    fprintf(stderr, "EDRAM word %u mismatch: %08lX != %08lX\n", x, (unsigned long)values[x], (unsigned long)golden[x]); status = E_FAIL; goto done;
  }
  values += words;
  for (x = 0; x < 64; ++x) {
    if (values[x] != counters[x]) { fprintf(stderr,"Counter %u mismatch: %lu != %u\n",x,(unsigned long)values[x],counters[x]); status = E_FAIL; goto done; }
  }  puts("Native guest ROV: all EDRAM words and query counters matched.");
done:
  if (mapped && readback) ID3D12Resource_Unmap(readback, 0, NULL);
  printf("WARP ROV: 0x%08lX\n", (unsigned long)status);
  if (event) CloseHandle(event);
  if (fence) ID3D12Fence_Release(fence);
  if (list) ID3D12GraphicsCommandList_Release(list);
  if (allocator) ID3D12CommandAllocator_Release(allocator);
  if (queue) ID3D12CommandQueue_Release(queue);
  if (pipeline) ID3D12PipelineState_Release(pipeline);
  if (sampled_texture) ID3D12Resource_Release(sampled_texture);
  if (sampled_upload) ID3D12Resource_Release(sampled_upload);
  if (sampler_heap) ID3D12DescriptorHeap_Release(sampler_heap);
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
  free(vs); free(ps); free(golden); free(initial); free(name);
  return FAILED(status) ? 1 : 0;
}
