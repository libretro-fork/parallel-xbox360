/* Execute native host-depth helpers on WARP. MSVC C89. */
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <initguid.h>
#ifdef __MINGW32__
#include <directx/d3d12.h>
#else
#include <d3d12.h>
#endif
#include <math.h>
#ifdef __MINGW32__
#include <directx/d3d12sdklayers.h>
#else
#include <d3d12sdklayers.h>
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
static unsigned sample_count = 1;
static HRESULT texture(ID3D12Device *device, D3D12_RESOURCE_FLAGS flags,
    D3D12_RESOURCE_STATES state, ID3D12Resource **resource) {
  D3D12_HEAP_PROPERTIES heap;
  D3D12_RESOURCE_DESC desc;
  memset(&heap, 0, sizeof(heap)); heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  memset(&desc, 0, sizeof(desc)); desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = 160; desc.Height = 48; desc.DepthOrArraySize = 1; desc.MipLevels = 1;
  desc.SampleDesc.Count = sample_count; desc.Format = (flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) ? DXGI_FORMAT_R32_TYPELESS : DXGI_FORMAT_R32G32B32A32_FLOAT; desc.Flags = flags;
  return ID3D12Device_CreateCommittedResource(device, &heap, D3D12_HEAP_FLAG_NONE,
    &desc, state, NULL, &IID_ID3D12Resource, (void **)resource);
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
static float gamma_value(float v) {
  float scale = 1023, offset = 0;
  if (!(v > 0)) v = 0;
  if (v > 1) v = 1;
  if (v >= 512.0f/1023.0f) { scale = 127.875f; offset = 128.0f/255.0f; }
  else if (v >= 128.0f/1023.0f) { scale = 255.75f; offset = 64.0f/255.0f; }
  else if (v >= 64.0f/1023.0f) { scale = 511.5f; offset = 32.0f/255.0f; }
  return (float)floor(v*scale)*(1.0f/255.0f)+offset;
}
static float blend_factor(unsigned factor, unsigned component, const float color[4]) {
  static const float blend[4] = {.0625f,.125f,.25f,.5f};
  switch (factor) {
  case 0: return 0; case 1: return 1;
  case 4: return color[component]; case 5: return 1-color[component];
  case 6: return color[3]; case 7: return 1-color[3];
  case 12: return blend[component]; case 13: return 1-blend[component];
  case 14: return blend[3]; default: return 1-blend[3];
  }
}
static uint32_t bits(float v) { uint32_t u; memcpy(&u,&v,4); return u; }
int main(int argc, char **argv) {
  static const float vertices[12] = {-1,1,.25f,1, 3,1,.25f,1, -1,-3,.25f,1};
  static const float constants[20] = {.125f,.25f,.5f,.75f, 1,2,3,4, -1,.0625f,.375f,1, .875f,.5f,.25f,.125f, .375f,6,7,8};
  ID3D12Debug *debug = NULL;
  ID3D12InfoQueue *info = NULL;
  IDXGIFactory4 *factory = NULL; IDXGIAdapter *adapter = NULL; ID3D12Device *device = NULL;
  ID3D12RootSignature *root = NULL; ID3D12PipelineState *pipeline = NULL;
  ID3D12CommandQueue *queue = NULL; ID3D12CommandAllocator *allocator = NULL;
  ID3D12GraphicsCommandList *list = NULL; ID3D12CommandList *submitted;
  ID3D12Resource *upload = NULL, *counter = NULL, *depth = NULL, *colors[4] = {NULL,NULL,NULL,NULL}, *resolves[4] = {NULL,NULL,NULL,NULL}, *readback = NULL;
  ID3D12DescriptorHeap *uav_heap = NULL, *rtv_heap = NULL, *dsv_heap = NULL;
  ID3D12Fence *fence = NULL; ID3DBlob *serialized = NULL, *errors = NULL;
  D3D12_ROOT_SIGNATURE_DESC root_desc; D3D12_ROOT_PARAMETER parameters[7]; D3D12_DESCRIPTOR_RANGE uav_range;
  D3D12_DESCRIPTOR_HEAP_DESC heap_desc; D3D12_UNORDERED_ACCESS_VIEW_DESC uav;
  D3D12_CPU_DESCRIPTOR_HANDLE cpu, rtvs[4], dsv; D3D12_GPU_DESCRIPTOR_HANDLE gpu;
  D3D12_GRAPHICS_PIPELINE_STATE_DESC desc; D3D12_INPUT_ELEMENT_DESC input[2];
  D3D12_COMMAND_QUEUE_DESC queue_desc; D3D12_VERTEX_BUFFER_VIEW vertex_view;
  D3D12_TEXTURE_COPY_LOCATION source, destination; D3D12_DEPTH_STENCIL_VIEW_DESC depth_view;
  D3D12_VIEWPORT viewport; D3D12_RECT scissor; D3D12_RANGE range;
  HANDLE event = NULL; void *ps = NULL, *vs = NULL, *mapped = NULL;
  uint32_t *data, expected_bits; float expected[4], unmultiplied[4], float_constants[20], clear[4] = {-99,-99,-99,-99};
  size_t ps_size = 0, vs_size = 0; unsigned guest_msaa = 0, native_2x, conditional_color, factor_variant, mode, variant, scenario, i, j, x, y, alpha_function = 7, fail, slot, count = 0;
  HRESULT status = E_FAIL;
  if (argc != 5 && argc != 6) return 2;
  mode = (unsigned)strtoul(argv[3],NULL,10); scenario = (unsigned)strtoul(argv[4],NULL,10);
  if (mode > 475 || scenario > 39) return 2;
  variant = mode/28; mode %= 28; native_2x = variant == 5; conditional_color = variant == 6; factor_variant = variant >= 7 ? variant-7 : UINT32_MAX; if (variant >= 5) variant = 0;
  if (argc == 6) guest_msaa = (unsigned)strtoul(argv[5],NULL,10);
  if (guest_msaa > 2) return 2;
  sample_count = guest_msaa ? native_2x && guest_msaa == 1 ? 2 : 4 : 1;
  memcpy(float_constants,constants,sizeof(constants));
  fail = (scenario == 1 && !conditional_color) || scenario == 2; slot = scenario == 7 ? 3 : 0;
  if (scenario == 1) alpha_function = 0;
  if (scenario == 2) memcpy(float_constants+16,constants,16);
  if (scenario >= 8) {
    alpha_function = (scenario-8)&7;
    if (scenario < 16) fail = !(alpha_function&2);
    else if (scenario < 24) {
      expected_bits = 0x7fc12345; memcpy(float_constants+3,&expected_bits,4);
      fail = alpha_function != 5 && alpha_function != 7;
    } else if (scenario < 32) fail = !(alpha_function&4);
    else fail = !(alpha_function&1);
    if (conditional_color) fail = 0;
  }
  ps = read_binary(argv[1],&ps_size); vs = read_binary(argv[2],&vs_size);
  if (!ps || !vs) goto done;
#define CHECK(e) do { status = (e); if (FAILED(status)) { fprintf(stderr,"Failed at line %d\n",__LINE__); goto done; } } while (0)
  if (SUCCEEDED(D3D12GetDebugInterface(&IID_ID3D12Debug,(void **)&debug))) ID3D12Debug_EnableDebugLayer(debug);
  CHECK(CreateDXGIFactory1(&IID_IDXGIFactory4,(void **)&factory));
  CHECK(IDXGIFactory4_EnumWarpAdapter(factory,&IID_IDXGIAdapter,(void **)&adapter));
  CHECK(D3D12CreateDevice((IUnknown *)adapter,D3D_FEATURE_LEVEL_11_0,&IID_ID3D12Device,(void **)&device));
  ID3D12Device_QueryInterface(device,&IID_ID3D12InfoQueue,(void **)&info);
  if (variant == 2) {
    D3D12_FEATURE_DATA_D3D12_OPTIONS3 features;
    memset(&features,0,sizeof(features));
    CHECK(ID3D12Device_CheckFeatureSupport(device,D3D12_FEATURE_D3D12_OPTIONS3,&features,sizeof(features)));
    printf("WARP barycentrics supported: %u\n",(unsigned)features.BarycentricsSupported);
  }
  memset(&root_desc,0,sizeof(root_desc)); memset(parameters,0,sizeof(parameters)); memset(&uav_range,0,sizeof(uav_range));
  uav_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV; uav_range.NumDescriptors = 1; uav_range.BaseShaderRegister = 1;
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[0].DescriptorTable.NumDescriptorRanges = 1; parameters[0].DescriptorTable.pDescriptorRanges = &uav_range;
  for (i = 1; i < 5; ++i) {
    parameters[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[i].Descriptor.ShaderRegister = i == 1 ? 0 : i; parameters[i].Descriptor.RegisterSpace = 1;
  }
  parameters[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV; parameters[5].Descriptor.ShaderRegister = 0;
  parameters[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; parameters[6].Descriptor.ShaderRegister = 1; parameters[6].Descriptor.RegisterSpace = 1;
  root_desc.NumParameters = 7; root_desc.pParameters = parameters; root_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
  CHECK(D3D12SerializeRootSignature(&root_desc,D3D_ROOT_SIGNATURE_VERSION_1,&serialized,&errors));
  CHECK(ID3D12Device_CreateRootSignature(device,0,ID3D10Blob_GetBufferPointer(serialized),ID3D10Blob_GetBufferSize(serialized),&IID_ID3D12RootSignature,(void **)&root));
  memset(&desc,0,sizeof(desc)); memset(input,0,sizeof(input));
  desc.pRootSignature = root; desc.VS.pShaderBytecode = vs; desc.VS.BytecodeLength = vs_size; desc.PS.pShaderBytecode = ps; desc.PS.BytecodeLength = ps_size;
  input[0].SemanticName = "POSITION"; input[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT; input[1] = input[0]; input[1].SemanticName = "TEXCOORD";
  desc.InputLayout.pInputElementDescs = input; desc.InputLayout.NumElements = 2;
  desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID; desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  desc.SampleMask = guest_msaa == 1 && !native_2x ? 9 : UINT_MAX; desc.SampleDesc.Count = sample_count; desc.NumRenderTargets = 4;
  desc.BlendState.IndependentBlendEnable = TRUE;
  for (i = 0; i < 4; ++i) { desc.RTVFormats[i] = DXGI_FORMAT_R32G32B32A32_FLOAT; desc.BlendState.RenderTarget[i].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL; }
  desc.DSVFormat = DXGI_FORMAT_D32_FLOAT; desc.DepthStencilState.DepthEnable = TRUE;
  desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL; desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
  CHECK(ID3D12Device_CreateGraphicsPipelineState(device,&desc,&IID_ID3D12PipelineState,(void **)&pipeline));
  CHECK(buffer(device,D3D12_HEAP_TYPE_UPLOAD,123392,D3D12_RESOURCE_STATE_GENERIC_READ,&upload));
  CHECK(buffer(device,D3D12_HEAP_TYPE_READBACK,614656,D3D12_RESOURCE_STATE_COPY_DEST,&readback));
  CHECK(buffer(device,D3D12_HEAP_TYPE_DEFAULT,256,D3D12_RESOURCE_STATE_COPY_DEST,&counter));
  CHECK(texture(device,D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,D3D12_RESOURCE_STATE_DEPTH_WRITE,&depth));
  for (i = 0; i < 4; ++i) CHECK(texture(device,D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,D3D12_RESOURCE_STATE_RENDER_TARGET,&colors[i]));
  if (sample_count > 1) {
    unsigned samples = sample_count; sample_count = 1;
    for (i = 0; i < 4; ++i) CHECK(texture(device,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_RESOLVE_DEST,&resolves[i]));
    sample_count = samples;
  }
  memset(&range,0,sizeof(range)); CHECK(ID3D12Resource_Map(upload,0,&range,&mapped)); memset(mapped,0,123392);
  memcpy((unsigned char *)mapped+122880,vertices,sizeof(vertices)); memcpy((unsigned char *)mapped+256,float_constants,sizeof(float_constants));
  data = (uint32_t *)mapped; data[0] = (alpha_function<<11)|(guest_msaa<<8)|(1u<<6)|(scenario == 3 ? 15u<<14 : 0);
  ((float *)data)[1] = scenario >= 8 && scenario < 16 ? .75f : scenario >= 32 ? 1 : .5f; data[2] = scenario == 5 ? 0x1e4 : 0; data[3] = scenario == 6 ? UINT32_MAX : slot;
  for (i = 0; i < 4; ++i) ((float *)data)[4+i] = scenario == 4 ? (float)(1u<<i) : 1;
  memcpy((unsigned char *)mapped+2048,vertices,sizeof(vertices));
  ((float *)data)[20] = 1; ((float *)data)[21] = 1; ((float *)data)[22] = 1;
  ((uint32_t *)mapped)[768/4] = 2; ((uint32_t *)mapped)[768/4+1] = sizeof(vertices);
  {
    static const float vertex_constants[12] = {.5f,-.5f,0,0, .5f,.5f,3,4, 4,4,4,4};
    memcpy((unsigned char *)mapped+2560,vertex_constants,sizeof(vertex_constants));
  }
  ((float *)data)[8] = .0625f; ((float *)data)[9] = .125f; ((float *)data)[10] = .25f; ((float *)data)[11] = .5f;
  ((float *)data)[14] = .125f; ((float *)data)[15] = .25f;
  ID3D12Resource_Unmap(upload,0,NULL); mapped = NULL;
  memset(&heap_desc,0,sizeof(heap_desc)); heap_desc.NumDescriptors = 1; heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  CHECK(ID3D12Device_CreateDescriptorHeap(device,&heap_desc,&IID_ID3D12DescriptorHeap,(void **)&uav_heap));
  ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(uav_heap,&cpu);

  memset(&uav,0,sizeof(uav)); uav.Format = DXGI_FORMAT_R32_TYPELESS; uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER; uav.Buffer.NumElements = 64; uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
  ID3D12Device_CreateUnorderedAccessView(device,counter,NULL,&uav,cpu);
  heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE; heap_desc.NumDescriptors = 4;
  CHECK(ID3D12Device_CreateDescriptorHeap(device,&heap_desc,&IID_ID3D12DescriptorHeap,(void **)&rtv_heap));
  ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(rtv_heap,&cpu);

  for (i = 0; i < 4; ++i) { rtvs[i] = cpu; ID3D12Device_CreateRenderTargetView(device,colors[i],NULL,cpu); cpu.ptr += ID3D12Device_GetDescriptorHandleIncrementSize(device,D3D12_DESCRIPTOR_HEAP_TYPE_RTV); }
  heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV; heap_desc.NumDescriptors = 1;
  CHECK(ID3D12Device_CreateDescriptorHeap(device,&heap_desc,&IID_ID3D12DescriptorHeap,(void **)&dsv_heap));
  ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(dsv_heap,&dsv);

  memset(&depth_view,0,sizeof(depth_view)); depth_view.Format = DXGI_FORMAT_D32_FLOAT; depth_view.ViewDimension = sample_count > 1 ? D3D12_DSV_DIMENSION_TEXTURE2DMS : D3D12_DSV_DIMENSION_TEXTURE2D;
  ID3D12Device_CreateDepthStencilView(device,depth,&depth_view,dsv);
  memset(&queue_desc,0,sizeof(queue_desc)); CHECK(ID3D12Device_CreateCommandQueue(device,&queue_desc,&IID_ID3D12CommandQueue,(void **)&queue));
  CHECK(ID3D12Device_CreateCommandAllocator(device,D3D12_COMMAND_LIST_TYPE_DIRECT,&IID_ID3D12CommandAllocator,(void **)&allocator));
  CHECK(ID3D12Device_CreateCommandList(device,0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator,pipeline,&IID_ID3D12GraphicsCommandList,(void **)&list));
  ID3D12GraphicsCommandList_CopyBufferRegion(list,counter,0,upload,123136,256); transition(list,counter,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  ID3D12GraphicsCommandList_SetGraphicsRootSignature(list,root); ID3D12GraphicsCommandList_SetDescriptorHeaps(list,1,&uav_heap);
  ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(uav_heap,&gpu);

  ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(list,0,gpu);
  for (i = 1; i < 5; ++i) ID3D12GraphicsCommandList_SetGraphicsRootConstantBufferView(list,i,ID3D12Resource_GetGPUVirtualAddress(upload)+(i-1)*256);
  ID3D12GraphicsCommandList_SetGraphicsRootShaderResourceView(list,5,ID3D12Resource_GetGPUVirtualAddress(upload)+2048);
  ID3D12GraphicsCommandList_SetGraphicsRootConstantBufferView(list,6,ID3D12Resource_GetGPUVirtualAddress(upload)+2560);
  memset(&viewport,0,sizeof(viewport)); viewport.Width = 160; viewport.Height = 48; viewport.MaxDepth = 1;
  scissor.left = 0; scissor.top = 0; scissor.right = 160; scissor.bottom = 48;
  ID3D12GraphicsCommandList_RSSetViewports(list,1,&viewport); ID3D12GraphicsCommandList_RSSetScissorRects(list,1,&scissor);
  for (i = 0; i < 4; ++i) ID3D12GraphicsCommandList_ClearRenderTargetView(list,rtvs[i],clear,0,NULL);
  ID3D12GraphicsCommandList_ClearDepthStencilView(list,dsv,D3D12_CLEAR_FLAG_DEPTH,.875f,0,0,NULL);
  ID3D12GraphicsCommandList_OMSetRenderTargets(list,4,rtvs,FALSE,&dsv);
  vertex_view.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(upload)+122880; vertex_view.SizeInBytes = sizeof(vertices); vertex_view.StrideInBytes = 16;
  ID3D12GraphicsCommandList_IASetVertexBuffers(list,0,1,&vertex_view); ID3D12GraphicsCommandList_IASetPrimitiveTopology(list,D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  ID3D12GraphicsCommandList_DrawInstanced(list,3,1,0,0);
  for (i = 0; i < 5; ++i) {
    if (i == 4 && sample_count > 1) continue;
    if (sample_count > 1) {
      transition(list,colors[i],D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
      ID3D12GraphicsCommandList_ResolveSubresource(list,resolves[i],0,colors[i],0,DXGI_FORMAT_R32G32B32A32_FLOAT);
      transition(list,resolves[i],D3D12_RESOURCE_STATE_RESOLVE_DEST,D3D12_RESOURCE_STATE_COPY_SOURCE);
    } else transition(list,i == 4 ? depth : colors[i],i == 4 ? D3D12_RESOURCE_STATE_DEPTH_WRITE : D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);
    memset(&source,0,sizeof(source)); source.pResource = i == 4 ? depth : sample_count > 1 ? resolves[i] : colors[i]; source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    memset(&destination,0,sizeof(destination)); destination.pResource = readback; destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint.Offset = i*122880; destination.PlacedFootprint.Footprint.Format = i == 4 ? DXGI_FORMAT_R32_TYPELESS : DXGI_FORMAT_R32G32B32A32_FLOAT;
    destination.PlacedFootprint.Footprint.Width = 160; destination.PlacedFootprint.Footprint.Height = 48; destination.PlacedFootprint.Footprint.Depth = 1; destination.PlacedFootprint.Footprint.RowPitch = 2560;
    ID3D12GraphicsCommandList_CopyTextureRegion(list,&destination,0,0,0,&source,NULL);
  }
  transition(list,counter,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE); ID3D12GraphicsCommandList_CopyBufferRegion(list,readback,614400,counter,0,256);
  CHECK(ID3D12GraphicsCommandList_Close(list)); submitted = (ID3D12CommandList *)list; ID3D12CommandQueue_ExecuteCommandLists(queue,1,&submitted);
  CHECK(ID3D12Device_CreateFence(device,0,D3D12_FENCE_FLAG_NONE,&IID_ID3D12Fence,(void **)&fence)); event = CreateEventA(NULL,FALSE,FALSE,NULL);
  if (!event) goto done;
  CHECK(ID3D12CommandQueue_Signal(queue,fence,1)); CHECK(ID3D12Fence_SetEventOnCompletion(fence,1,event));
  if (WaitForSingleObject(event,10000) != WAIT_OBJECT_0) goto done;
  range.Begin = 0; range.End = 614656; CHECK(ID3D12Resource_Map(readback,0,&range,&mapped)); data = (uint32_t *)mapped;
  for (y = 0; y < 48; ++y) for (x = 0; x < 160; ++x) {
    unsigned offset = (x&1)*2+(y&1), passed_samples = fail ? 0 : 1u<<guest_msaa;
    unsigned passed;
    if (!fail && scenario == 5 && !conditional_color) passed_samples = guest_msaa == 0 ? offset >= 1 : guest_msaa == 1 ? 1+(offset >= 2) : 3;
    passed = passed_samples != 0; count += passed_samples;
    for (i = 0; i < 5; ++i) {
      if (i == 4 && sample_count > 1) continue;
      for (j = 0; j < 4; ++j) expected[j] = conditional_color && !i ? 0 : float_constants[i*4+j];
      if (i == 1 && ((mode%14 >= 7 && !variant) || variant == 3)) { expected[0] = (float)x; expected[1] = (float)y; expected[2] = 0; expected[3] = 0;
        if (variant == 3) { expected[1] = -expected[1]; expected[2] = ((float)x+.5f)/160; expected[3] = ((float)y+.5f)/48; }
      }
      if (i == 1 && variant && variant != 3) { expected[0] = ((float)x+.5f)/160; expected[1] = ((float)y+.5f)/48; expected[2] = 3; expected[3] = 4; if (variant == 4) for (j = 0; j < 4; ++j) expected[j] += 4; }
      if (i < 4) for (j = 0; j < 4; ++j) { if (scenario == 4) expected[j] *= (float)(1u<<i);
  if (scenario == 3 && j < 3) expected[j] = gamma_value(expected[j]); }
      else { expected[0] = mode >= 14 ? .375f : .25f;
  if (mode < 14 && mode%7 >= 4) expected[0] += .125f;
  if (mode%7 == 2 || mode%7 == 3 || mode%7 == 5 || mode%7 == 6) { if (mode < 14) expected[0] = expected[0]*2 > 1 ? 1 : expected[0]*2; expected[0] *= .5f; } }
      if (!i && factor_variant != UINT32_MAX) {
        static const unsigned factors[10] = {0,1,4,5,6,7,12,13,14,15};
        memcpy(unmultiplied,expected,sizeof(expected));
        for (j = 0; j < 4; ++j) expected[j] *= blend_factor(factors[j == 3 ? 9-factor_variant : factor_variant],j,unmultiplied);
      }
      for (j = 0; j < (i == 4 ? 1u : 4u); ++j) {
        if (i < 4 && sample_count > 1) expected[j] = (expected[j]*(float)passed_samples-99.0f*(float)(sample_count-passed_samples))/(float)sample_count;
        expected_bits = bits(passed ? expected[j] : i == 4 ? .875f : -99);
        /* Floating resolve may discard NaN contributions. Other lanes and
         * the exact counter still verify whether the NaN alpha test passed. */
        if (sample_count > 1 && i < 4 && (expected_bits&0x7fffffff)>0x7f800000) continue;
        if (data[i*30720+y*640+x*(i == 4 ? 1 : 4)+j] != expected_bits && !((expected_bits&0x7fffffff)>0x7f800000 && (data[i*30720+y*640+x*(i == 4 ? 1 : 4)+j]&0x7fffffff)>0x7f800000) && !(passed && i == 1 && variant && (scenario != 3 || j == 3) && fabs((double)((float *)data)[i*30720+y*640+x*4+j]-expected[j]) < .00001) && !(sample_count > 1 && i < 4 && fabs((double)((float *)data)[i*30720+y*640+x*4+j]-expected[j]) <= .000002*(fabs((double)expected[j])+1))) { fprintf(stderr,"RT%u %u,%u component%u: %08lX != %08lX\n",i,x,y,j,(unsigned long)data[i*30720+y*640+x*(i == 4 ? 1 : 4)+j],(unsigned long)expected_bits); status = E_FAIL; goto done; }
      }
    }
  }
  for (i = 0; i < 64; ++i) if (data[153600+i] != (i == slot*4 && scenario != 6 ? count : 0)) { fprintf(stderr,"Counter mismatch %u: %lu != %u\n",i,(unsigned long)data[153600+i],count); status = E_FAIL; goto done; }
  puts("Native guest pixel: four render targets, depth and coverage counter matched."); status = S_OK;
done:
  if (info && FAILED(status)) {
    UINT64 m;
    for (m = 0; m < ID3D12InfoQueue_GetNumStoredMessages(info); ++m) {
      SIZE_T size = 0; D3D12_MESSAGE *message;
      ID3D12InfoQueue_GetMessage(info,m,NULL,&size); message = (D3D12_MESSAGE *)malloc(size);
      if (message) { if (SUCCEEDED(ID3D12InfoQueue_GetMessage(info,m,message,&size))) fprintf(stderr,"%s\n",message->pDescription); free(message); }
    }
  }
  if (info) ID3D12InfoQueue_Release(info);
  if (debug) ID3D12Debug_Release(debug);
  if (mapped && readback) ID3D12Resource_Unmap(readback,0,NULL);
  printf("WARP guest pixel: 0x%08lX\n",(unsigned long)status);
  if (event) CloseHandle(event);
  if (fence) ID3D12Fence_Release(fence);
  if (list) ID3D12GraphicsCommandList_Release(list);
  if (allocator) ID3D12CommandAllocator_Release(allocator);
  if (queue) ID3D12CommandQueue_Release(queue);
  if (pipeline) ID3D12PipelineState_Release(pipeline);
  if (uav_heap) ID3D12DescriptorHeap_Release(uav_heap);
  if (rtv_heap) ID3D12DescriptorHeap_Release(rtv_heap);
  if (dsv_heap) ID3D12DescriptorHeap_Release(dsv_heap);
  if (readback) ID3D12Resource_Release(readback);
  if (depth) ID3D12Resource_Release(depth);
  for (i = 0; i < 4; ++i) {
    if (colors[i]) ID3D12Resource_Release(colors[i]);
    if (resolves[i]) ID3D12Resource_Release(resolves[i]);
  }
  if (counter) ID3D12Resource_Release(counter);
  if (upload) ID3D12Resource_Release(upload);
  if (root) ID3D12RootSignature_Release(root);
  if (errors) ID3D10Blob_Release(errors);
  if (serialized) ID3D10Blob_Release(serialized);
  if (device) ID3D12Device_Release(device);
  if (adapter) IDXGIAdapter_Release(adapter);
  if (factory) IDXGIFactory4_Release(factory);
  free(vs); free(ps); return FAILED(status) ? 1 : 0;
}
