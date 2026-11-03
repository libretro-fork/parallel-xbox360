/* Execute complete vertex/pixel memory exporters on WARP. MSVC C89. */
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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(call) do { status = (call); if (FAILED(status)) { fprintf(stderr,"Line %u: %08lX\n",(unsigned)__LINE__,(unsigned long)status); goto done; } } while (0)
#define RELEASE(p,type) do { if (p) type##_Release(p); } while (0)
static void *read_file(const char *base, const char *suffix, size_t *size) {
  char path[2048]; FILE *f; long n; void *p;
  if (strlen(base)+strlen(suffix) >= sizeof(path)) return NULL;
  strcpy(path,base); strcat(path,suffix); f = fopen(path,"rb"); if (!f) return NULL;
  if (fseek(f,0,SEEK_END) || (n = ftell(f)) <= 0 || fseek(f,0,SEEK_SET)) { fclose(f); return NULL; }
  p = malloc((size_t)n); if (!p || fread(p,1,(size_t)n,f) != (size_t)n) { free(p); p = NULL; }
  fclose(f); *size = (size_t)n; return p;
}
static HRESULT buffer(ID3D12Device *d, D3D12_HEAP_TYPE heap_type, UINT64 size,
    D3D12_RESOURCE_STATES state, unsigned uav, ID3D12Resource **out) {
  D3D12_HEAP_PROPERTIES heap; D3D12_RESOURCE_DESC desc;
  memset(&heap,0,sizeof(heap)); memset(&desc,0,sizeof(desc)); heap.Type = heap_type;
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; desc.Width = size; desc.Height = 1;
  desc.DepthOrArraySize = 1; desc.MipLevels = 1; desc.SampleDesc.Count = 1; desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  if (uav) desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  return ID3D12Device_CreateCommittedResource(d,&heap,D3D12_HEAP_FLAG_NONE,&desc,state,NULL,&IID_ID3D12Resource,(void **)out);
}
static int domain_output(const unsigned char *mapped, unsigned config) {
  const float *values = (const float *)mapped; uint64_t bytes; unsigned n, j, corner, seen[4] = {0,0,0,0};
  float r0[4], expected[12]; const float *coordinates; unsigned level = config/4 ? 2 : 1, quad, patch;
  config %= 4; quad = config >= 2; patch = config&1;
  memcpy(&bytes,mapped+8192,8);
  /* Fractional-even triangle factor 2 makes a center fan of six triangles;
   * the quad has a 2x2 grid (eight triangles). */
  if (bytes != (uint64_t)(level == 1 ? quad ? 6 : 3 : quad ? 24 : 18)*64) { fprintf(stderr,"Domain stream bytes: %lu\n",(unsigned long)bytes); return 0; }
  for (n = 0; n < bytes/64; ++n) {
    memcpy(r0,values+n*16+4,16); coordinates = values+n*16+12;
    expected[0] = expected[1] = 0; expected[2] = .5f; expected[3] = 1;
    for (j = 4; j < 12; ++j) expected[j] = 0;
    if (!quad) {
      if (coordinates[0]+coordinates[1]+coordinates[2] != 1) return 0;
      corner = coordinates[0] == 1 ? 0 : coordinates[1] == 1 ? 1 : coordinates[2] == 1 ? 2 : 4;
      expected[4] = coordinates[2]; expected[5] = coordinates[1]; expected[6] = coordinates[0]; expected[7] = 1; expected[8] = 17;
      if (!patch) { expected[9] = 18; expected[10] = 19; }
    } else {
      float u = r0[patch ? 1 : 0], v = r0[patch ? 2 : 1];
      if (u != coordinates[0] || v != coordinates[1]) return 0;
      corner = (u == 0 || u == 1) && (v == 0 || v == 1) ? (unsigned)u+2*(unsigned)v : 4;
      if (patch) { expected[4] = 17; expected[5] = u; expected[6] = v; expected[7] = 1; }
      else { expected[4] = u; expected[5] = v; expected[6] = 17; expected[8] = 18; expected[9] = 19; expected[10] = 20; }
    }
    for (j = 0; j < (quad ? 2u : 3u); ++j) if (coordinates[j] < 0 || coordinates[j] > 1) return 0;
    for (j = 0; j < 12; ++j) if (values[n*16+j] != expected[j]) { fprintf(stderr,"Domain vertex %u lane %u: %g != %g\n",n,j,values[n*16+j],expected[j]); return 0; }
    if (corner < 4) ++seen[corner];
  }
  for (j = 0; j < (quad ? 4u : 3u); ++j) if (!seen[j]) return 0;
  return 1;
}
int main(int argc, char **argv) {
  IDXGIFactory4 *factory = NULL; IDXGIAdapter *adapter = NULL; ID3D12Device *device = NULL;
  ID3D12Debug *debug = NULL; ID3D12InfoQueue *info = NULL;
  ID3D12RootSignature *root = NULL; ID3D12PipelineState *pipeline = NULL;
  ID3D12CommandQueue *queue = NULL; ID3D12CommandAllocator *allocator = NULL;
  ID3D12GraphicsCommandList *list = NULL; ID3D12CommandList *commands[1]; ID3D12Fence *fence = NULL;
  ID3D12Resource *upload = NULL, *output = NULL, *readback = NULL;
  ID3D12Resource *domain_memory = NULL, *domain_readback = NULL;
  ID3DBlob *serialized = NULL, *errors = NULL; HANDLE event = NULL;
  D3D12_ROOT_SIGNATURE_DESC root_desc; D3D12_ROOT_PARAMETER parameters[10];
  D3D12_SO_DECLARATION_ENTRY declarations[4]; D3D12_STREAM_OUTPUT_BUFFER_VIEW stream; UINT stride = 64;
  D3D12_GRAPHICS_PIPELINE_STATE_DESC pso; D3D12_COMMAND_QUEUE_DESC queue_desc;
  D3D12_RESOURCE_BARRIER barrier; D3D12_VIEWPORT viewport; D3D12_RECT scissor; D3D12_RANGE range;
  unsigned char *mapped = NULL; void *vs = NULL, *ps = NULL, *constants = NULL, *system = NULL, *expected = NULL;
  size_t vs_size = 0, ps_size = 0, constants_size = 0, system_size = 0, expected_size = 0, hs_size = 0, ds_size = 0;
  size_t constant_offset, initial_offset, output_bytes, upload_bytes; void *hs = NULL, *ds = NULL;
  unsigned i, domain_mode = argc == 4, config = 0, points = 1; HRESULT status = E_FAIL;
  if (argc < 2 || argc > 4) return 2;
  if (domain_mode) { config = (unsigned)strtoul(argv[3],NULL,0); if (config > 11) return 2; points = config >= 8 ? config%4 < 2 ? 3 : 4 : config&1 ? 1 : config%4 < 2 ? 3 : 4; }
  vs = read_file(argv[argc >= 3 ? 2 : 1],domain_mode ? ".host.vs" : ".vs",&vs_size);
  if (domain_mode) { hs = read_file(argv[2],".hs",&hs_size); ds = read_file(argv[1],".ds",&ds_size); }
  else ps = read_file(argv[argc == 3 ? 2 : 1],".ps",&ps_size);
  constants = read_file(argv[1],".constants",&constants_size); system = read_file(argv[argc >= 3 ? 2 : 1],".system",&system_size);
  expected = read_file(argv[1],".expected",&expected_size);
  if (!vs || (domain_mode ? (!hs || !ds) : !ps) || !constants || !system || !expected || constants_size != 256 || (system_size != 256 && system_size != 768) || expected_size != 128) goto done;
  constant_offset = system_size; initial_offset = constant_offset+256; output_bytes = domain_mode ? 8320 : 128; upload_bytes = initial_offset+output_bytes+(domain_mode ? 128 : 0);
  if (SUCCEEDED(D3D12GetDebugInterface(&IID_ID3D12Debug,(void **)&debug))) ID3D12Debug_EnableDebugLayer(debug);
  CHECK(CreateDXGIFactory1(&IID_IDXGIFactory4,(void **)&factory));
  CHECK(IDXGIFactory4_EnumWarpAdapter(factory,&IID_IDXGIAdapter,(void **)&adapter));
  CHECK(D3D12CreateDevice((IUnknown *)adapter,D3D_FEATURE_LEVEL_11_0,&IID_ID3D12Device,(void **)&device));
  ID3D12Device_QueryInterface(device,&IID_ID3D12InfoQueue,(void **)&info);
  memset(&root_desc,0,sizeof(root_desc)); memset(parameters,0,sizeof(parameters));
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  for (i = 2; i < 7; ++i) { parameters[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; parameters[i].Descriptor.RegisterSpace = 1; parameters[i].Descriptor.ShaderRegister = i-2; }
  parameters[7].ParameterType = parameters[8].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
  parameters[7].Descriptor.ShaderRegister = 1; parameters[8].Descriptor.ShaderRegister = 2;
  root_desc.NumParameters = 9; root_desc.pParameters = parameters;
  if (domain_mode) {
    parameters[9].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; root_desc.NumParameters = 10;
    root_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_STREAM_OUTPUT;
  }
  CHECK(D3D12SerializeRootSignature(&root_desc,D3D_ROOT_SIGNATURE_VERSION_1,&serialized,&errors));
  CHECK(ID3D12Device_CreateRootSignature(device,0,ID3D10Blob_GetBufferPointer(serialized),ID3D10Blob_GetBufferSize(serialized),&IID_ID3D12RootSignature,(void **)&root));
  memset(&pso,0,sizeof(pso)); pso.pRootSignature = root;
  pso.VS.pShaderBytecode = vs; pso.VS.BytecodeLength = vs_size; pso.PS.pShaderBytecode = ps; pso.PS.BytecodeLength = ps_size;
  pso.SampleMask = UINT_MAX; pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID; pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  pso.RasterizerState.DepthClipEnable = TRUE; pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT; pso.SampleDesc.Count = 1;
  if (domain_mode) {
    pso.HS.pShaderBytecode = hs; pso.HS.BytecodeLength = hs_size; pso.DS.pShaderBytecode = ds; pso.DS.BytecodeLength = ds_size;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
    memset(declarations,0,sizeof(declarations));
    for (i = 0; i < 4; ++i) { declarations[i].SemanticName = i ? "TEXCOORD" : "SV_Position"; declarations[i].SemanticIndex = i ? i-1 : 0; declarations[i].ComponentCount = 4; }
    pso.StreamOutput.pSODeclaration = declarations; pso.StreamOutput.NumEntries = 4; pso.StreamOutput.pBufferStrides = &stride;
    pso.StreamOutput.NumStrides = 1; pso.StreamOutput.RasterizedStream = D3D12_SO_NO_RASTERIZED_STREAM;
  }
  CHECK(ID3D12Device_CreateGraphicsPipelineState(device,&pso,&IID_ID3D12PipelineState,(void **)&pipeline));
  CHECK(buffer(device,D3D12_HEAP_TYPE_UPLOAD,upload_bytes,D3D12_RESOURCE_STATE_GENERIC_READ,0,&upload));
  CHECK(buffer(device,D3D12_HEAP_TYPE_DEFAULT,output_bytes,D3D12_RESOURCE_STATE_COPY_DEST,1,&output));
  CHECK(buffer(device,D3D12_HEAP_TYPE_READBACK,output_bytes,D3D12_RESOURCE_STATE_COPY_DEST,0,&readback));
  if (domain_mode) {
    CHECK(buffer(device,D3D12_HEAP_TYPE_DEFAULT,128,D3D12_RESOURCE_STATE_COPY_DEST,1,&domain_memory));
    CHECK(buffer(device,D3D12_HEAP_TYPE_READBACK,128,D3D12_RESOURCE_STATE_COPY_DEST,0,&domain_readback));
  }
  memset(&range,0,sizeof(range)); CHECK(ID3D12Resource_Map(upload,0,&range,(void **)&mapped));
  memset(mapped,0,upload_bytes); memcpy(mapped,system,system_size); memcpy(mapped+constant_offset,constants,256);
  if (!domain_mode) for (i = 0; i < 32; ++i) ((uint32_t *)(mapped+initial_offset))[i] = 0xa5c39e71;
  if (domain_mode) for (i = 0; i < 32; ++i) ((uint32_t *)(mapped+initial_offset+output_bytes))[i] = 0xa5c39e71;
  ID3D12Resource_Unmap(upload,0,NULL); mapped = NULL;
  memset(&queue_desc,0,sizeof(queue_desc)); CHECK(ID3D12Device_CreateCommandQueue(device,&queue_desc,&IID_ID3D12CommandQueue,(void **)&queue));
  CHECK(ID3D12Device_CreateCommandAllocator(device,D3D12_COMMAND_LIST_TYPE_DIRECT,&IID_ID3D12CommandAllocator,(void **)&allocator));
  CHECK(ID3D12Device_CreateCommandList(device,0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator,pipeline,&IID_ID3D12GraphicsCommandList,(void **)&list));
  ID3D12GraphicsCommandList_CopyBufferRegion(list,output,0,upload,initial_offset,output_bytes);
  memset(&barrier,0,sizeof(barrier)); barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; barrier.Transition.pResource = output;
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST; barrier.Transition.StateAfter = domain_mode ? D3D12_RESOURCE_STATE_STREAM_OUT : D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  ID3D12GraphicsCommandList_ResourceBarrier(list,1,&barrier);
  if (domain_mode) {
    ID3D12GraphicsCommandList_CopyBufferRegion(list,domain_memory,0,upload,initial_offset+output_bytes,128);
    barrier.Transition.pResource = domain_memory; barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    ID3D12GraphicsCommandList_ResourceBarrier(list,1,&barrier); barrier.Transition.pResource = output;
  }
  ID3D12GraphicsCommandList_SetGraphicsRootSignature(list,root);
  ID3D12GraphicsCommandList_SetGraphicsRootUnorderedAccessView(list,0,ID3D12Resource_GetGPUVirtualAddress(domain_mode ? domain_memory : output));
  ID3D12GraphicsCommandList_SetGraphicsRootShaderResourceView(list,1,ID3D12Resource_GetGPUVirtualAddress(domain_mode ? domain_memory : output));
  for (i = 2; i < 7; ++i) ID3D12GraphicsCommandList_SetGraphicsRootConstantBufferView(list,i,ID3D12Resource_GetGPUVirtualAddress(upload)+(i == 2 ? 0 : constant_offset));
  ID3D12GraphicsCommandList_SetGraphicsRootUnorderedAccessView(list,7,ID3D12Resource_GetGPUVirtualAddress(domain_mode ? domain_memory : output));
  ID3D12GraphicsCommandList_SetGraphicsRootUnorderedAccessView(list,8,ID3D12Resource_GetGPUVirtualAddress(domain_mode ? domain_memory : output));
  memset(&viewport,0,sizeof(viewport)); viewport.Width = viewport.Height = 1; viewport.MaxDepth = 1;
  if (((uint32_t *)system)[63]) { viewport.Width = (float)(((uint32_t *)system)[63]&255); viewport.Height = (float)(((uint32_t *)system)[63]>>8); }
  scissor.left = scissor.top = 0; scissor.right = (LONG)viewport.Width; scissor.bottom = (LONG)viewport.Height;
  ID3D12GraphicsCommandList_RSSetViewports(list,1,&viewport); ID3D12GraphicsCommandList_RSSetScissorRects(list,1,&scissor);
  ID3D12GraphicsCommandList_IASetPrimitiveTopology(list,D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
  if (domain_mode) {
    ID3D12GraphicsCommandList_SetGraphicsRootConstantBufferView(list,9,ID3D12Resource_GetGPUVirtualAddress(upload));
    memset(&stream,0,sizeof(stream)); stream.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(output); stream.SizeInBytes = 8192; stream.BufferFilledSizeLocation = stream.BufferLocation+8192;
    ID3D12GraphicsCommandList_SOSetTargets(list,0,1,&stream);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(list,(D3D_PRIMITIVE_TOPOLOGY)(D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST+points-1));
  }
  ID3D12GraphicsCommandList_DrawInstanced(list,domain_mode ? points : 1,1,0,0);
  barrier.Transition.StateBefore = domain_mode ? D3D12_RESOURCE_STATE_STREAM_OUT : D3D12_RESOURCE_STATE_UNORDERED_ACCESS; barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  ID3D12GraphicsCommandList_ResourceBarrier(list,1,&barrier); ID3D12GraphicsCommandList_CopyBufferRegion(list,readback,0,output,0,output_bytes);
  if (domain_mode) {
    barrier.Transition.pResource = domain_memory; barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    ID3D12GraphicsCommandList_ResourceBarrier(list,1,&barrier);
    ID3D12GraphicsCommandList_CopyBufferRegion(list,domain_readback,0,domain_memory,0,128);
  }
  CHECK(ID3D12GraphicsCommandList_Close(list)); commands[0] = (ID3D12CommandList *)list; ID3D12CommandQueue_ExecuteCommandLists(queue,1,commands);
  CHECK(ID3D12Device_CreateFence(device,0,D3D12_FENCE_FLAG_NONE,&IID_ID3D12Fence,(void **)&fence));
  event = CreateEvent(NULL,FALSE,FALSE,NULL); if (!event) { status = E_FAIL; goto done; }
  CHECK(ID3D12CommandQueue_Signal(queue,fence,1)); CHECK(ID3D12Fence_SetEventOnCompletion(fence,1,event));
  if (WaitForSingleObject(event,30000) != WAIT_OBJECT_0) { status = E_FAIL; goto done; }
  range.Begin = 0; range.End = output_bytes; CHECK(ID3D12Resource_Map(readback,0,&range,(void **)&mapped));
  if (domain_mode) {
    i = (unsigned)domain_output(mapped,config); ID3D12Resource_Unmap(readback,0,NULL); mapped = NULL;
    if (!i) { status = E_FAIL; goto done; }
  }
  else for (i = 0; i < 32; ++i) if (((uint32_t *)mapped)[i] != ((uint32_t *)expected)[i]) {
    fprintf(stderr,"Word %u: %08lX != %08lX\n",i,(unsigned long)((uint32_t *)mapped)[i],(unsigned long)((uint32_t *)expected)[i]); status = E_FAIL; goto done;
  }
  if (domain_mode) {
    range.End = 128;
    CHECK(ID3D12Resource_Map(domain_readback,0,&range,(void **)&mapped));
    for (i = 0; i < 32; ++i) if (((uint32_t *)mapped)[i] != ((uint32_t *)expected)[i]) {
      fprintf(stderr,"Domain export word %u: %08lX != %08lX\n",i,(unsigned long)((uint32_t *)mapped)[i],(unsigned long)((uint32_t *)expected)[i]);
      status = E_FAIL;
    }
    ID3D12Resource_Unmap(domain_readback,0,NULL); mapped = NULL;
    if (FAILED(status)) goto done;
  }
  puts(domain_mode ? "Native guest domain: stream output and memory exports matched." : "Native guest graphics exports: all 128 bytes matched.");
done:
  if (mapped) ID3D12Resource_Unmap(domain_mode ? domain_readback : readback,0,NULL);
  if (info) {
    UINT64 n, total = ID3D12InfoQueue_GetNumStoredMessages(info);
    for (n = 0; n < total; ++n) {
      SIZE_T length = 0; D3D12_MESSAGE *message;
      ID3D12InfoQueue_GetMessage(info,n,NULL,&length); message = (D3D12_MESSAGE *)malloc(length);
      if (message && SUCCEEDED(ID3D12InfoQueue_GetMessage(info,n,message,&length)) && message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR) { fprintf(stderr,"%s\n",message->pDescription); status = E_FAIL; }
      free(message);
    }
  }
  if (event) CloseHandle(event);
  RELEASE(fence,ID3D12Fence); RELEASE(list,ID3D12GraphicsCommandList); RELEASE(allocator,ID3D12CommandAllocator); RELEASE(queue,ID3D12CommandQueue);
  RELEASE(domain_memory,ID3D12Resource); RELEASE(domain_readback,ID3D12Resource);
  RELEASE(upload,ID3D12Resource); RELEASE(output,ID3D12Resource); RELEASE(readback,ID3D12Resource);
  RELEASE(pipeline,ID3D12PipelineState); RELEASE(root,ID3D12RootSignature); RELEASE(serialized,ID3D10Blob); RELEASE(errors,ID3D10Blob);
  RELEASE(info,ID3D12InfoQueue); RELEASE(device,ID3D12Device); RELEASE(adapter,IDXGIAdapter); RELEASE(factory,IDXGIFactory4); RELEASE(debug,ID3D12Debug);
  free(vs); free(ps); free(hs); free(ds); free(constants); free(system); free(expected); return FAILED(status) ? 1 : 0;
}
