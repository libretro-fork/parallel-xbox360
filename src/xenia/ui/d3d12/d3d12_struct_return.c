/* C ABI adapters avoid compiler-specific C++ COM structure-return rules.
 * MSVC C89-compatible. BSD license, see LICENSE. */
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "d3d12_struct_return.h"

D3D12_CPU_DESCRIPTOR_HANDLE xe_d3d12_heap_cpu(ID3D12DescriptorHeap *heap) {
  D3D12_CPU_DESCRIPTOR_HANDLE result;
  ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap, &result);
  return result;

}
D3D12_GPU_DESCRIPTOR_HANDLE xe_d3d12_heap_gpu(ID3D12DescriptorHeap *heap) {
  D3D12_GPU_DESCRIPTOR_HANDLE result;
  ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap, &result);
  return result;

}
D3D12_RESOURCE_DESC xe_d3d12_resource_desc(ID3D12Resource *resource) {
  D3D12_RESOURCE_DESC result;
  ID3D12Resource_GetDesc(resource, &result);
  return result;

}
D3D12_HEAP_DESC xe_d3d12_heap_desc(ID3D12Heap *heap) {
  D3D12_HEAP_DESC result;
  ID3D12Heap_GetDesc(heap, &result);
  return result;

}
D3D12_RESOURCE_ALLOCATION_INFO xe_d3d12_allocation_info(ID3D12Device *device,
  UINT mask, UINT count, const D3D12_RESOURCE_DESC *descs) {
  D3D12_RESOURCE_ALLOCATION_INFO result;
  ID3D12Device_GetResourceAllocationInfo(device, &result, mask, count, descs);
  return result;

}
