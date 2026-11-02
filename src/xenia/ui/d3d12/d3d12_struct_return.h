/* C ABI adapters for Direct3D methods returning structures. MSVC C89. */
#ifndef XE_D3D12_STRUCT_RETURN_H
#define XE_D3D12_STRUCT_RETURN_H
#include "third_party/DirectX-Headers/include/directx/d3d12.h"
#ifdef __cplusplus
extern "C" {
#endif
D3D12_CPU_DESCRIPTOR_HANDLE xe_d3d12_heap_cpu(ID3D12DescriptorHeap *heap);
D3D12_GPU_DESCRIPTOR_HANDLE xe_d3d12_heap_gpu(ID3D12DescriptorHeap *heap);
D3D12_RESOURCE_DESC xe_d3d12_resource_desc(ID3D12Resource *resource);
D3D12_HEAP_DESC xe_d3d12_heap_desc(ID3D12Heap *heap);
D3D12_RESOURCE_ALLOCATION_INFO xe_d3d12_allocation_info(ID3D12Device *device,
  UINT mask, UINT count, const D3D12_RESOURCE_DESC *descs);
#ifdef __cplusplus
}
#endif
#endif
