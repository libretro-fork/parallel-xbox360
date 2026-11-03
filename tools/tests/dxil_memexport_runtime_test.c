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
int main(int argc, char **argv) {
  IDXGIFactory4 *factory = NULL; IDXGIAdapter *adapter = NULL; ID3D12Device *device = NULL;
  ID3D12Debug *debug = NULL; ID3D12InfoQueue *info = NULL;
  ID3D12RootSignature *root = NULL; ID3D12PipelineState *pipeline = NULL;
  ID3D12CommandQueue *queue = NULL; ID3D12CommandAllocator *allocator = NULL;
  ID3D12GraphicsCommandList *list = NULL; ID3D12CommandList *commands[1]; ID3D12Fence *fence = NULL;
  ID3D12Resource *upload = NULL, *output = NULL, *readback = NULL;
  ID3DBlob *serialized = NULL, *errors = NULL; HANDLE event = NULL;
  D3D12_ROOT_SIGNATURE_DESC root_desc; D3D12_ROOT_PARAMETER parameters[9];
  D3D12_GRAPHICS_PIPELINE_STATE_DESC pso; D3D12_COMMAND_QUEUE_DESC queue_desc;
  D3D12_RESOURCE_BARRIER barrier; D3D12_VIEWPORT viewport; D3D12_RECT scissor; D3D12_RANGE range;
  unsigned char *mapped = NULL; void *vs = NULL, *ps = NULL, *constants = NULL, *system = NULL, *expected = NULL;
  size_t vs_size = 0, ps_size = 0, constants_size = 0, system_size = 0, expected_size = 0;
  unsigned i; HRESULT status = E_FAIL;
  if (argc < 2 || argc > 3) return 2;
  vs = read_file(argv[argc == 3 ? 2 : 1],".vs",&vs_size); ps = read_file(argv[argc == 3 ? 2 : 1],".ps",&ps_size);
  constants = read_file(argv[1],".constants",&constants_size); system = read_file(argv[argc == 3 ? 2 : 1],".system",&system_size);
  expected = read_file(argv[1],".expected",&expected_size);
  if (!vs || !ps || !constants || !system || !expected || constants_size != 256 || system_size != 256 || expected_size != 128) goto done;
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
  CHECK(D3D12SerializeRootSignature(&root_desc,D3D_ROOT_SIGNATURE_VERSION_1,&serialized,&errors));
  CHECK(ID3D12Device_CreateRootSignature(device,0,ID3D10Blob_GetBufferPointer(serialized),ID3D10Blob_GetBufferSize(serialized),&IID_ID3D12RootSignature,(void **)&root));
  memset(&pso,0,sizeof(pso)); pso.pRootSignature = root;
  pso.VS.pShaderBytecode = vs; pso.VS.BytecodeLength = vs_size; pso.PS.pShaderBytecode = ps; pso.PS.BytecodeLength = ps_size;
  pso.SampleMask = UINT_MAX; pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID; pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  pso.RasterizerState.DepthClipEnable = TRUE; pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT; pso.SampleDesc.Count = 1;
  CHECK(ID3D12Device_CreateGraphicsPipelineState(device,&pso,&IID_ID3D12PipelineState,(void **)&pipeline));
  CHECK(buffer(device,D3D12_HEAP_TYPE_UPLOAD,768,D3D12_RESOURCE_STATE_GENERIC_READ,0,&upload));
  CHECK(buffer(device,D3D12_HEAP_TYPE_DEFAULT,128,D3D12_RESOURCE_STATE_COPY_DEST,1,&output));
  CHECK(buffer(device,D3D12_HEAP_TYPE_READBACK,128,D3D12_RESOURCE_STATE_COPY_DEST,0,&readback));
  memset(&range,0,sizeof(range)); CHECK(ID3D12Resource_Map(upload,0,&range,(void **)&mapped));
  memcpy(mapped,system,256); memcpy(mapped+256,constants,256);
  for (i = 0; i < 32; ++i) ((uint32_t *)(mapped+512))[i] = 0xa5c39e71;
  ID3D12Resource_Unmap(upload,0,NULL); mapped = NULL;
  memset(&queue_desc,0,sizeof(queue_desc)); CHECK(ID3D12Device_CreateCommandQueue(device,&queue_desc,&IID_ID3D12CommandQueue,(void **)&queue));
  CHECK(ID3D12Device_CreateCommandAllocator(device,D3D12_COMMAND_LIST_TYPE_DIRECT,&IID_ID3D12CommandAllocator,(void **)&allocator));
  CHECK(ID3D12Device_CreateCommandList(device,0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator,pipeline,&IID_ID3D12GraphicsCommandList,(void **)&list));
  ID3D12GraphicsCommandList_CopyBufferRegion(list,output,0,upload,512,128);
  memset(&barrier,0,sizeof(barrier)); barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; barrier.Transition.pResource = output;
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST; barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  ID3D12GraphicsCommandList_ResourceBarrier(list,1,&barrier);
  ID3D12GraphicsCommandList_SetGraphicsRootSignature(list,root);
  ID3D12GraphicsCommandList_SetGraphicsRootUnorderedAccessView(list,0,ID3D12Resource_GetGPUVirtualAddress(output));
  ID3D12GraphicsCommandList_SetGraphicsRootShaderResourceView(list,1,ID3D12Resource_GetGPUVirtualAddress(output));
  for (i = 2; i < 7; ++i) ID3D12GraphicsCommandList_SetGraphicsRootConstantBufferView(list,i,ID3D12Resource_GetGPUVirtualAddress(upload)+(i == 2 ? 0 : 256));
  ID3D12GraphicsCommandList_SetGraphicsRootUnorderedAccessView(list,7,ID3D12Resource_GetGPUVirtualAddress(output));
  ID3D12GraphicsCommandList_SetGraphicsRootUnorderedAccessView(list,8,ID3D12Resource_GetGPUVirtualAddress(output));
  memset(&viewport,0,sizeof(viewport)); viewport.Width = viewport.Height = 1; viewport.MaxDepth = 1;
  if (((uint32_t *)system)[63]) { viewport.Width = (float)(((uint32_t *)system)[63]&255); viewport.Height = (float)(((uint32_t *)system)[63]>>8); }
  scissor.left = scissor.top = 0; scissor.right = (LONG)viewport.Width; scissor.bottom = (LONG)viewport.Height;
  ID3D12GraphicsCommandList_RSSetViewports(list,1,&viewport); ID3D12GraphicsCommandList_RSSetScissorRects(list,1,&scissor);
  ID3D12GraphicsCommandList_IASetPrimitiveTopology(list,D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
  ID3D12GraphicsCommandList_DrawInstanced(list,1,1,0,0);
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS; barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  ID3D12GraphicsCommandList_ResourceBarrier(list,1,&barrier); ID3D12GraphicsCommandList_CopyBufferRegion(list,readback,0,output,0,128);
  CHECK(ID3D12GraphicsCommandList_Close(list)); commands[0] = (ID3D12CommandList *)list; ID3D12CommandQueue_ExecuteCommandLists(queue,1,commands);
  CHECK(ID3D12Device_CreateFence(device,0,D3D12_FENCE_FLAG_NONE,&IID_ID3D12Fence,(void **)&fence));
  event = CreateEvent(NULL,FALSE,FALSE,NULL); if (!event) { status = E_FAIL; goto done; }
  CHECK(ID3D12CommandQueue_Signal(queue,fence,1)); CHECK(ID3D12Fence_SetEventOnCompletion(fence,1,event));
  if (WaitForSingleObject(event,30000) != WAIT_OBJECT_0) { status = E_FAIL; goto done; }
  range.Begin = 0; range.End = 128; CHECK(ID3D12Resource_Map(readback,0,&range,(void **)&mapped));
  for (i = 0; i < 32; ++i) if (((uint32_t *)mapped)[i] != ((uint32_t *)expected)[i]) {
    fprintf(stderr,"Word %u: %08lX != %08lX\n",i,(unsigned long)((uint32_t *)mapped)[i],(unsigned long)((uint32_t *)expected)[i]); status = E_FAIL; goto done;
  }
  puts("Native guest graphics exports: all 128 bytes matched.");
done:
  if (mapped && readback) ID3D12Resource_Unmap(readback,0,NULL);
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
  RELEASE(upload,ID3D12Resource); RELEASE(output,ID3D12Resource); RELEASE(readback,ID3D12Resource);
  RELEASE(pipeline,ID3D12PipelineState); RELEASE(root,ID3D12RootSignature); RELEASE(serialized,ID3D10Blob); RELEASE(errors,ID3D10Blob);
  RELEASE(info,ID3D12InfoQueue); RELEASE(device,ID3D12Device); RELEASE(adapter,IDXGIAdapter); RELEASE(factory,IDXGIFactory4); RELEASE(debug,ID3D12Debug);
  free(vs); free(ps); free(constants); free(system); free(expected); return FAILED(status) ? 1 : 0;
}
