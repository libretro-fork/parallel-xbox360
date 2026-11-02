/* Execute native guest vertex shaders through WARP stream output. MSVC C89. */
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <initguid.h>
#ifdef __MINGW32__
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
static uint32_t swapped(uint32_t v, unsigned endian) {
  switch (endian) {
  case 1: return ((v&0x00ff00ff)<<8)|((v&0xff00ff00)>>8);
  case 2: return (v<<24)|((v&0xff00)<<8)|((v>>8)&0xff00)|(v>>24);
  case 3: return (v<<16)|(v>>16); default: return v;
  }
}
int main(int argc, char **argv) {
  static const float vertices[4][4] = {{1,2,.25f,1},{-2,4,.5f,2},{.5f,-1,1,4},{4,2,2,.5f}};
  static const float planes[6][4] = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1},{.5f,.25f,0,.125f},{0,0,.5f,-.25f}};
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
  D3D12_ROOT_PARAMETER parameters[5];
  D3D12_GRAPHICS_PIPELINE_STATE_DESC desc;
  D3D12_SO_DECLARATION_ENTRY declarations[8];
  D3D12_COMMAND_QUEUE_DESC queue_desc;
  D3D12_STREAM_OUTPUT_BUFFER_VIEW output_view;
  D3D12_RANGE range;
  HANDLE event = NULL;
  void *vs = NULL, *mapped = NULL;
  uint32_t *words, expected_bits, actual_bits, id;
  float expected[16], original[4], *values, w, index_float;
  size_t vs_size = 0;
  UINT stride;
  UINT64 written;
  unsigned config, flags, endian, kill, clips, culls, point, i, j, k, count, n, entry_count, components, interpolators;
  int32_t base;
  HRESULT status = E_FAIL;
  if (argc != 7) return 2;
  config = (unsigned)strtoul(argv[2],NULL,10); flags = (unsigned)strtoul(argv[3],NULL,10);
  endian = (unsigned)strtoul(argv[4],NULL,10); base = (int32_t)strtol(argv[5],NULL,10); kill = (unsigned)strtoul(argv[6],NULL,10);
  if (config > 5 || flags > 7 || endian > 3 || kill > 3) return 2;
  clips = config == 1 ? 6 : config == 3 ? 3 : 0;
  culls = config == 1 ? 1 : config == 2 ? 7 : 0;
  point = config != 0 && config != 5; interpolators = config == 5 ? 3 : 1; components = 4+interpolators*4+clips+culls+point; stride = components*4;
  vs = read_binary(argv[1],&vs_size); if (!vs) goto done;
#define CHECK(expression) do { status = (expression); if (FAILED(status)) { fprintf(stderr,"Failed at line %d\n",__LINE__); goto done; } } while (0)
  CHECK(CreateDXGIFactory1(&IID_IDXGIFactory4,(void **)&factory));
  CHECK(IDXGIFactory4_EnumWarpAdapter(factory,&IID_IDXGIAdapter,(void **)&adapter));
  CHECK(D3D12CreateDevice((IUnknown *)adapter,D3D_FEATURE_LEVEL_11_0,&IID_ID3D12Device,(void **)&device));
  memset(&root_desc,0,sizeof(root_desc)); memset(parameters,0,sizeof(parameters));
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  for (i = 1; i < 5; ++i) {
    parameters[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; parameters[i].Descriptor.RegisterSpace = 1;
    parameters[i].Descriptor.ShaderRegister = i == 1 ? 0 : i == 2 ? 1 : i;
  }
  root_desc.NumParameters = 5; root_desc.pParameters = parameters; root_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_STREAM_OUTPUT;
  CHECK(D3D12SerializeRootSignature(&root_desc,D3D_ROOT_SIGNATURE_VERSION_1,&serialized,&errors));
  CHECK(ID3D12Device_CreateRootSignature(device,0,ID3D10Blob_GetBufferPointer(serialized),ID3D10Blob_GetBufferSize(serialized),&IID_ID3D12RootSignature,(void **)&root));
  memset(&desc,0,sizeof(desc)); memset(declarations,0,sizeof(declarations));
  desc.pRootSignature = root; desc.VS.pShaderBytecode = vs; desc.VS.BytecodeLength = vs_size;
  entry_count = 0;
  declarations[entry_count].SemanticName = "SV_Position"; declarations[entry_count++].ComponentCount = 4;
  for (i = 0; i < clips; i += 4) {
    declarations[entry_count].SemanticName = "SV_ClipDistance"; declarations[entry_count].SemanticIndex = i/4;
    declarations[entry_count++].ComponentCount = (BYTE)(clips-i > 4 ? 4 : clips-i);
  }
  for (i = 0; i < culls; i += 4) {
    declarations[entry_count].SemanticName = "SV_CullDistance"; declarations[entry_count].SemanticIndex = i/4;
    declarations[entry_count++].ComponentCount = (BYTE)(culls-i > 4 ? 4 : culls-i);
  }
  for (i = 0; i < interpolators; ++i) {
    declarations[entry_count].SemanticName = "TEXCOORD"; declarations[entry_count].SemanticIndex = i; declarations[entry_count++].ComponentCount = 4;
  }
  if (point) { declarations[entry_count].SemanticName = "TEXCOORD"; declarations[entry_count].SemanticIndex = 1; declarations[entry_count++].ComponentCount = 1; }
  desc.StreamOutput.pSODeclaration = declarations; desc.StreamOutput.NumEntries = entry_count;
  desc.StreamOutput.pBufferStrides = &stride; desc.StreamOutput.NumStrides = 1; desc.StreamOutput.RasterizedStream = D3D12_SO_NO_RASTERIZED_STREAM;
  desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
  desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID; desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  desc.SampleMask = UINT_MAX; desc.SampleDesc.Count = 1;
  CHECK(ID3D12Device_CreateGraphicsPipelineState(device,&desc,&IID_ID3D12PipelineState,(void **)&pipeline));
  CHECK(buffer(device,D3D12_HEAP_TYPE_UPLOAD,2304,D3D12_RESOURCE_STATE_GENERIC_READ,&upload));
  CHECK(buffer(device,D3D12_HEAP_TYPE_DEFAULT,1024,D3D12_RESOURCE_STATE_STREAM_OUT,&output));
  CHECK(buffer(device,D3D12_HEAP_TYPE_READBACK,1024,D3D12_RESOURCE_STATE_COPY_DEST,&readback));
  memset(&range,0,sizeof(range)); CHECK(ID3D12Resource_Map(upload,0,&range,&mapped)); memset(mapped,0,2304); memcpy(mapped,vertices,sizeof(vertices));
  words = (uint32_t *)((unsigned char *)mapped+256); words[0] = ((flags&1)?1u<<5:0)|((flags&2)?1u<<3:0)|((flags&4)?1u<<4:0);
  words[1] = endian; words[2] = (uint32_t)base;
  ((float *)words)[4] = 2; ((float *)words)[5] = -.5f; ((float *)words)[6] = .25f;
  ((float *)words)[8] = .125f; ((float *)words)[9] = .25f; ((float *)words)[10] = -.5f;
  memcpy(words+16,planes,sizeof(planes));
  words = (uint32_t *)((unsigned char *)mapped+512); ((float *)words)[0] = 2;
  for (i = 0; i < 4; ++i) ((float *)words)[4+i] = (float)(11+i);
  words[2] = kill == 1 ? 0x3f800000 : kill == 2 ? 0x80000000 : kill == 3 ? 1 : 0;
  words = (uint32_t *)((unsigned char *)mapped+1024); words[0] = 2; words[1] = 64;
  ID3D12Resource_Unmap(upload,0,NULL); mapped = NULL;
  memset(&queue_desc,0,sizeof(queue_desc));
  CHECK(ID3D12Device_CreateCommandQueue(device,&queue_desc,&IID_ID3D12CommandQueue,(void **)&queue));
  CHECK(ID3D12Device_CreateCommandAllocator(device,D3D12_COMMAND_LIST_TYPE_DIRECT,&IID_ID3D12CommandAllocator,(void **)&allocator));
  CHECK(ID3D12Device_CreateCommandList(device,0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator,pipeline,&IID_ID3D12GraphicsCommandList,(void **)&list));
  transition(list,output,D3D12_RESOURCE_STATE_STREAM_OUT,D3D12_RESOURCE_STATE_COPY_DEST);
  ID3D12GraphicsCommandList_CopyBufferRegion(list,output,0,upload,2048,8);
  transition(list,output,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_STREAM_OUT);
  ID3D12GraphicsCommandList_SetGraphicsRootSignature(list,root);
  ID3D12GraphicsCommandList_SetGraphicsRootShaderResourceView(list,0,ID3D12Resource_GetGPUVirtualAddress(upload));
  ID3D12GraphicsCommandList_SetGraphicsRootConstantBufferView(list,1,ID3D12Resource_GetGPUVirtualAddress(upload)+256);
  ID3D12GraphicsCommandList_SetGraphicsRootConstantBufferView(list,2,ID3D12Resource_GetGPUVirtualAddress(upload)+512);
  ID3D12GraphicsCommandList_SetGraphicsRootConstantBufferView(list,3,ID3D12Resource_GetGPUVirtualAddress(upload)+768);
  ID3D12GraphicsCommandList_SetGraphicsRootConstantBufferView(list,4,ID3D12Resource_GetGPUVirtualAddress(upload)+1024);
  memset(&output_view,0,sizeof(output_view)); output_view.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(output)+256;
  output_view.SizeInBytes = 512; output_view.BufferFilledSizeLocation = ID3D12Resource_GetGPUVirtualAddress(output);
  ID3D12GraphicsCommandList_SOSetTargets(list,0,1,&output_view);
  ID3D12GraphicsCommandList_IASetPrimitiveTopology(list,D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
  ID3D12GraphicsCommandList_DrawInstanced(list,4,1,0,0);
  transition(list,output,D3D12_RESOURCE_STATE_STREAM_OUT,D3D12_RESOURCE_STATE_COPY_SOURCE);
  ID3D12GraphicsCommandList_CopyResource(list,readback,output); CHECK(ID3D12GraphicsCommandList_Close(list));
  submitted = (ID3D12CommandList *)list; ID3D12CommandQueue_ExecuteCommandLists(queue,1,&submitted);
  CHECK(ID3D12Device_CreateFence(device,0,D3D12_FENCE_FLAG_NONE,&IID_ID3D12Fence,(void **)&fence));
  event = CreateEventA(NULL,FALSE,FALSE,NULL); if (!event) { status = E_FAIL; goto done; }
  CHECK(ID3D12CommandQueue_Signal(queue,fence,1)); CHECK(ID3D12Fence_SetEventOnCompletion(fence,1,event));
  if (WaitForSingleObject(event,10000) != WAIT_OBJECT_0) { status = E_FAIL; goto done; }
  range.Begin = 0; range.End = 1024; CHECK(ID3D12Resource_Map(readback,0,&range,&mapped));
  memcpy(&written,mapped,8); if (written != 4*stride) { fprintf(stderr,"Unexpected SO byte count: %lu\n",(unsigned long)written); status = E_FAIL; goto done; }
  values = (float *)((unsigned char *)mapped+256);
  for (i = 0; i < 4; ++i) {
    id = swapped(i,endian)+(uint32_t)base; index_float = (float)(int32_t)id;
    for (j = 0; j < 4; ++j) original[j] = id < 4 ? vertices[id][j] : 0;
    w = flags&1 ? original[3] : 1.0f/original[3]; original[3] = w;
    if (flags&2) { original[0] *= w; original[1] *= w; } if (flags&4) original[2] *= w;
    expected[0] = original[0]*2+.125f*w; expected[1] = original[1]*-.5f+.25f*w; expected[2] = original[2]*.25f-.5f*w;
    expected[3] = w;
    if (config == 3 && (kill == 1 || kill == 3)) { expected_bits = 0x7fc00000; memcpy(&expected[3],&expected_bits,4); }
    n = 4;
    count = clips ? clips : config == 2 ? 6 : 0;
    for (j = 0; j < count; ++j) { expected[n] = 0; for (k = 0; k < 4; ++k) expected[n] += original[k]*planes[j][k]; ++n; }
    if (config == 1 || config == 2) expected[n++] = kill == 1 || kill == 3 ? -1.0f : 0.0f;
    expected[n++] = index_float; expected[n++] = 0; expected[n++] = 0; expected[n++] = 0;
    if (config == 5) {
      for (j = 0; j < 4; ++j) expected[n++] = (float)(11+j);
      for (j = 0; j < 4; ++j) expected[n++] = id < 4 ? vertices[id][j] : 0;
    }
    if (point) expected[n++] = config == 4 ? -1.0f : 2.0f;
    if (n != components) { status = E_FAIL; goto done; }
    for (j = 0; j < n; ++j) {
      memcpy(&expected_bits,expected+j,4); memcpy(&actual_bits,values+i*n+j,4);
      if (expected_bits == actual_bits || ((expected_bits&0x7fffffff)>0x7f800000 && (actual_bits&0x7fffffff)>0x7f800000)) continue;
      fprintf(stderr,"Vertex %u lane %u: %08lX != %08lX\n",i,j,(unsigned long)actual_bits,(unsigned long)expected_bits); status = E_FAIL; goto done;
    }
  }
  puts("Native guest vertex: all positions, clip/cull distances and exports matched.");
done:
  if (mapped && readback) ID3D12Resource_Unmap(readback,0,NULL);
  printf("WARP guest vertex: 0x%08lX\n",(unsigned long)status);
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
  free(vs); return FAILED(status) ? 1 : 0;
}
