/* Exercise real COM structure returns on WARP. MSVC C89-compatible. */
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <dxgi1_4.h>
#include "xenia/ui/d3d12/d3d12_struct_return.h"
#include <stdio.h>
#ifdef __cplusplus
#define XE_TEST_IID(name) name
#else
#define XE_TEST_IID(name) (&name)
#endif

int main(void) {
  IDXGIFactory4 *factory = NULL;
  IDXGIAdapter *adapter = NULL;
  ID3D12Device *device = NULL;
  ID3D12Resource *resource = NULL;
  ID3D12Heap *heap = NULL;
  ID3D12DescriptorHeap *descriptors = NULL;
  D3D12_HEAP_PROPERTIES properties;
  D3D12_HEAP_DESC heap_desc, actual_heap;
  D3D12_RESOURCE_DESC desc, actual_desc;
  D3D12_DESCRIPTOR_HEAP_DESC descriptor_desc;
  D3D12_CPU_DESCRIPTOR_HANDLE cpu;
  D3D12_GPU_DESCRIPTOR_HANDLE gpu;
  D3D12_RESOURCE_ALLOCATION_INFO info;
  HRESULT status;
  int result = 1;
  status = CreateDXGIFactory1(XE_TEST_IID(IID_IDXGIFactory4), (void **)&factory);
  if (FAILED(status)) goto done;
  status = IDXGIFactory4_EnumWarpAdapter(factory, XE_TEST_IID(IID_IDXGIAdapter), (void **)&adapter);
  if (FAILED(status)) goto done;
  status = D3D12CreateDevice((IUnknown *)adapter, D3D_FEATURE_LEVEL_11_0,
    XE_TEST_IID(IID_ID3D12Device), (void **)&device);
  if (FAILED(status)) goto done;
  memset(&properties, 0, sizeof(properties)); properties.Type = D3D12_HEAP_TYPE_UPLOAD;
  properties.CreationNodeMask = 1; properties.VisibleNodeMask = 1;
  memset(&desc, 0, sizeof(desc)); desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = 256; desc.Height = 1; desc.DepthOrArraySize = 1; desc.MipLevels = 1;
  desc.SampleDesc.Count = 1; desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  status = ID3D12Device_CreateCommittedResource(device, &properties, D3D12_HEAP_FLAG_NONE,
    &desc, D3D12_RESOURCE_STATE_GENERIC_READ, NULL, XE_TEST_IID(IID_ID3D12Resource), (void **)&resource);
  if (FAILED(status)) goto done;
  actual_desc = xe_d3d12_resource_desc(resource);
  if (actual_desc.Width != 256 || actual_desc.Height != 1 || actual_desc.MipLevels != 1 ||
      actual_desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER ||
      actual_desc.Layout != D3D12_TEXTURE_LAYOUT_ROW_MAJOR) goto done;
  info = xe_d3d12_allocation_info(device, 0, 1, &desc);
  if (info.Alignment < 65536 || info.SizeInBytes < 256 || info.SizeInBytes == ~(UINT64)0) goto done;
  memset(&heap_desc, 0, sizeof(heap_desc));
  heap_desc.SizeInBytes = 65536; heap_desc.Properties = properties;
  heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
  status = ID3D12Device_CreateHeap(device, &heap_desc, XE_TEST_IID(IID_ID3D12Heap), (void **)&heap);
  if (FAILED(status)) goto done;
  actual_heap = xe_d3d12_heap_desc(heap);
  if (actual_heap.SizeInBytes != 65536 || actual_heap.Properties.Type != D3D12_HEAP_TYPE_UPLOAD ||
      actual_heap.Flags != D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS) goto done;
  memset(&descriptor_desc, 0, sizeof(descriptor_desc));
  descriptor_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  descriptor_desc.NumDescriptors = 1; descriptor_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  status = ID3D12Device_CreateDescriptorHeap(device, &descriptor_desc,
    XE_TEST_IID(IID_ID3D12DescriptorHeap), (void **)&descriptors);
  if (FAILED(status)) goto done;
  cpu = xe_d3d12_heap_cpu(descriptors); gpu = xe_d3d12_heap_gpu(descriptors);
  if (!cpu.ptr || !gpu.ptr) goto done;
  result = 0;
  puts("D3D12 structure returns: all five adapters matched.");
done:
  if (descriptors) ID3D12DescriptorHeap_Release(descriptors);
  if (heap) ID3D12Heap_Release(heap);
  if (resource) ID3D12Resource_Release(resource);
  if (device) ID3D12Device_Release(device);
  if (adapter) IDXGIAdapter_Release(adapter);
  if (factory) IDXGIFactory4_Release(factory);
  return result;
}
