/* Execute native geometry shaders through WARP stream output. MSVC C89. */
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
  static const float vertices[16] = {-.5f,.5f,.25f,1, .5f,.5f,.25f,1,
    -.5f,-.5f,.25f,1, .5f,-.5f,.25f,1};
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
  D3D12_ROOT_PARAMETER parameter;
  D3D12_GRAPHICS_PIPELINE_STATE_DESC desc;
  D3D12_INPUT_ELEMENT_DESC input;
  D3D12_SO_DECLARATION_ENTRY declarations[2];
  D3D12_COMMAND_QUEUE_DESC queue_desc;
  D3D12_VERTEX_BUFFER_VIEW vertex_view;
  D3D12_STREAM_OUTPUT_BUFFER_VIEW output_view;
  D3D12_RANGE range;
  HANDLE event = NULL;
  void *gs = NULL, *vs = NULL, *mapped = NULL;
  float *constants, *values;
  float expected[4][4];
  size_t gs_size = 0, vs_size = 0, length;
  UINT stride = 32;
  UINT64 written;
  unsigned type, i, j, k, seen = 0;
  int pixel_interface;
  char *vs_path = NULL;
  HRESULT status = E_FAIL;
  if (argc != 3 && argc != 4) return 2;
  pixel_interface = argc == 4 && !strcmp(argv[3], "pixel");
  if (argc == 4 && strcmp(argv[3], "discard") && !pixel_interface) return 2;
  type = (unsigned)strtoul(argv[2], NULL, 10);
  if ((!pixel_interface && type < 1) || type > 4) return 2;
  gs = read_binary(argv[1], &gs_size);
  length = strlen(argv[1]); vs_path = (char *)malloc(length + 4);
  if (!gs || !vs_path) goto done;
  memcpy(vs_path, argv[1], length); memcpy(vs_path + length, ".vs", 4);
  vs = read_binary(vs_path, &vs_size);
  if (!vs) goto done;
  status = CreateDXGIFactory1(&IID_IDXGIFactory4, (void **)&factory);
  if (FAILED(status)) goto done;
  status = IDXGIFactory4_EnumWarpAdapter(factory, &IID_IDXGIAdapter, (void **)&adapter);
  if (FAILED(status)) goto done;
  status = D3D12CreateDevice((IUnknown *)adapter, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&device);
  if (FAILED(status)) goto done;
  memset(&root_desc, 0, sizeof(root_desc)); memset(&parameter, 0, sizeof(parameter));
  parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; parameter.Descriptor.RegisterSpace = 1;
  root_desc.NumParameters = 1; root_desc.pParameters = &parameter;
  root_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT | D3D12_ROOT_SIGNATURE_FLAG_ALLOW_STREAM_OUTPUT;
  status = D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors);
  if (FAILED(status)) goto done;
  status = ID3D12Device_CreateRootSignature(device, 0, ID3D10Blob_GetBufferPointer(serialized),
    ID3D10Blob_GetBufferSize(serialized), &IID_ID3D12RootSignature, (void **)&root);
  if (FAILED(status)) goto done;
  memset(&desc, 0, sizeof(desc)); memset(&input, 0, sizeof(input)); memset(declarations, 0, sizeof(declarations));
  desc.pRootSignature = root; desc.VS.pShaderBytecode = vs; desc.VS.BytecodeLength = vs_size;
  if (pixel_interface) { desc.PS.pShaderBytecode = gs; desc.PS.BytecodeLength = gs_size; }
  else { desc.GS.pShaderBytecode = gs; desc.GS.BytecodeLength = gs_size; }
  input.SemanticName = "POSITION"; input.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
  desc.InputLayout.pInputElementDescs = &input; desc.InputLayout.NumElements = 1;
  declarations[0].SemanticName = "SV_Position"; declarations[0].ComponentCount = 4;
  declarations[1].SemanticName = "TEXCOORD"; declarations[1].ComponentCount = 4;
  desc.StreamOutput.pSODeclaration = declarations; desc.StreamOutput.NumEntries = 2;
  desc.StreamOutput.pBufferStrides = &stride; desc.StreamOutput.NumStrides = 1;
  desc.StreamOutput.RasterizedStream = D3D12_SO_NO_RASTERIZED_STREAM;
  desc.PrimitiveTopologyType = type == 1 ? D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT :
    type == 2 ? D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE : D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
  desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID; desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  desc.RasterizerState.DepthClipEnable = TRUE; desc.SampleMask = UINT_MAX; desc.SampleDesc.Count = 1;
  if (pixel_interface) {
    memset(&desc.StreamOutput, 0, sizeof(desc.StreamOutput));
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    if (type == 2 || type == 3) {
      desc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
      desc.DepthStencilState.DepthEnable = TRUE;
      desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
      desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
      desc.DepthStencilState.StencilEnable = type == 3;
      desc.DepthStencilState.StencilReadMask = 255; desc.DepthStencilState.StencilWriteMask = 255;
      desc.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
      desc.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
      desc.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_REPLACE;
      desc.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
      desc.DepthStencilState.BackFace = desc.DepthStencilState.FrontFace;
    } else {
      desc.NumRenderTargets = 1;
      desc.RTVFormats[0] = type ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R32G32B32A32_FLOAT;
    }
    if (type == 4) desc.SampleDesc.Count = 4;
  }
  status = ID3D12Device_CreateGraphicsPipelineState(device, &desc, &IID_ID3D12PipelineState, (void **)&pipeline);
  if (FAILED(status)) goto done;
  if (pixel_interface) { puts("Native DXIL pixel interface: WARP pipeline accepted."); goto done; }
  status = buffer(device, D3D12_HEAP_TYPE_UPLOAD, 8192, D3D12_RESOURCE_STATE_GENERIC_READ, &upload);
  if (FAILED(status)) goto done;
  status = buffer(device, D3D12_HEAP_TYPE_DEFAULT, 1024, D3D12_RESOURCE_STATE_COPY_DEST, &output);
  if (FAILED(status)) goto done;
  status = buffer(device, D3D12_HEAP_TYPE_READBACK, 1024, D3D12_RESOURCE_STATE_COPY_DEST, &readback);
  if (FAILED(status)) goto done;
  range.Begin = 0; range.End = 0;
  status = ID3D12Resource_Map(upload, 0, &range, &mapped);
  if (FAILED(status)) goto done;
  memset(mapped, 0, 8192); memcpy(mapped, vertices, sizeof(vertices));
  constants = (float *)((unsigned char *)mapped + 256);
  constants[20] = 2; constants[21] = 2; constants[22] = .25f; constants[23] = .25f;
  ID3D12Resource_Unmap(upload, 0, NULL); mapped = NULL;
  memset(&queue_desc, 0, sizeof(queue_desc));
  status = ID3D12Device_CreateCommandQueue(device, &queue_desc, &IID_ID3D12CommandQueue, (void **)&queue);
  if (FAILED(status)) goto done;
  status = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void **)&allocator);
  if (FAILED(status)) goto done;
  status = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, pipeline,
    &IID_ID3D12GraphicsCommandList, (void **)&list);
  if (FAILED(status)) goto done;
  ID3D12GraphicsCommandList_CopyBufferRegion(list, output, 0, upload, 128, 8);
  transition(list, output, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_STREAM_OUT);
  vertex_view.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(upload);
  vertex_view.SizeInBytes = sizeof(vertices); vertex_view.StrideInBytes = 16;
  output_view.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(output) + 256;
  output_view.SizeInBytes = 768; output_view.BufferFilledSizeLocation = ID3D12Resource_GetGPUVirtualAddress(output);
  ID3D12GraphicsCommandList_SetGraphicsRootSignature(list, root);
  ID3D12GraphicsCommandList_SetGraphicsRootConstantBufferView(list, 0, ID3D12Resource_GetGPUVirtualAddress(upload) + 256);
  ID3D12GraphicsCommandList_IASetVertexBuffers(list, 0, 1, &vertex_view);
  ID3D12GraphicsCommandList_IASetPrimitiveTopology(list, type == 1 ? D3D_PRIMITIVE_TOPOLOGY_POINTLIST :
    type == 2 ? D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST : type == 3 ? D3D_PRIMITIVE_TOPOLOGY_LINELIST_ADJ : D3D_PRIMITIVE_TOPOLOGY_LINELIST);
  ID3D12GraphicsCommandList_SOSetTargets(list, 0, 1, &output_view);
  ID3D12GraphicsCommandList_DrawInstanced(list, type == 1 ? 1 : type == 2 ? 3 : type == 3 ? 4 : 2, 1, 0, 0);
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
  range.Begin = 0; range.End = 1024;
  status = ID3D12Resource_Map(readback, 0, &range, &mapped);
  if (FAILED(status)) goto done;
  memcpy(&written, mapped, sizeof(written));
  if (argc == 4) {
    if (written) { fprintf(stderr, "Discarded primitive wrote stream output\n"); status = E_FAIL; }
    else puts("Native DXIL geometry: primitive discard matched.");
    goto done;
  }
  if (written != 192) { fprintf(stderr, "Unexpected stream bytes: %lu\n", (unsigned long)written); status = E_FAIL; goto done; }
  memcpy(expected, vertices, sizeof(expected));
  if (type == 1) for (i = 0; i < 4; ++i) {
    expected[i][0] = i & 2 ? 0.0f : -1.0f; expected[i][1] = i & 1 ? 1.0f : 0.0f;
  }
  if (type == 4) for (i = 0; i < 4; ++i) {
    expected[i][0] = i & 2 ? .5f : -.5f; expected[i][1] = i & 1 ? .75f : .25f;
  }
  values = (float *)((unsigned char *)mapped + 256);
  for (i = 0; i < 6; ++i) {
    for (j = 0; j < 4; ++j) if (!memcmp(values + i * 8, expected[j], 16)) break;
    if (j == 4) { fprintf(stderr, "Unexpected vertex %u: %g %g %g %g\n", i,
      values[i*8], values[i*8+1], values[i*8+2], values[i*8+3]); status = E_FAIL; goto done; }
    seen |= 1u << j;
    for (k = 0; k < 4; ++k) if (values[i * 8 + 4 + k] != (float)(k + 1)) { status = E_FAIL; goto done; }
  }
  if (seen != 15) { status = E_FAIL; goto done; }
  puts("Native DXIL geometry: six stream-output vertices and attributes matched.");
done:
  if (mapped && readback) ID3D12Resource_Unmap(readback, 0, NULL);
  printf("D3D12 WARP geometry: 0x%08lX\n", (unsigned long)status);
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
  free(gs); free(vs); free(vs_path);
  return FAILED(status) ? 1 : 0;
}
